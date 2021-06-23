#!/bin/bash

#set -e
#set -x

script_dir=$(dirname $0)

usb_enum_pause_s=5
jlink_options="-device ATSAME51J19 -if swd -JTAGConf -1,-1 -speed auto"

usage()
{
	echo $(basename $0) \[OPTIONS\] BL_FW_FILE APP_FW_FILE
	echo
	echo
}



#https://stackoverflow.com/questions/192249/how-do-i-parse-command-line-arguments-in-bash
POSITIONAL=()
while [ $# -gt 0 ]; do
	key="$1"

 	case $key in
 		-h|--help)
			usage
			exit 0
			;;
		*)    # unknown option
			POSITIONAL+=("$1") # save it in an array for later
			shift # past argument
 			;;
 	esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

if [ ${#POSITIONAL[@]} -ne 2 ]; then
	echo "ERROR: $(basename $0) BL_FW_FILE APP_FW_FILE"
	exit 1
fi

bl_fw_file=${POSITIONAL[0]}
app_fw_file=${POSITIONAL[1]}

if [ ! -f "$bl_fw_file" ]; then
	echo "ERROR: no such file $bl_fw_file"
	exit 1
fi

if [ ! -f "$app_fw_file" ]; then
	echo "ERROR: no such file $app_fw_file"
	exit 1
fi


if [ 0 -ne $(id -u) ]; then
	echo ERROR: $(basename $0) must be run as root!
	exit 1
fi


tmp_dir=
date_str=$(date +"%F_%H%M%S")

cleanup()
{
	if [ ! -z "$tmp_dir" ] && [ -d "$tmp_dir" ]; then
		echo INFO: Removing $tmp_dir
		rm -rf "$tmp_dir"
	fi
}

error_cleanup()
{
	local last=$_
	local rc=$?
	if [ 0 -ne $rc ]; then
		echo ERROR: command $last failed with $rc
	fi

	cleanup
}

pack_results()
{
	local tar_file=flash-test-result-${date_str}.tar.zst

	echo
	echo INFO: creating archive $tar_file
	tar -C "$tmp_dir" -c --exclude="$tar_file" . | zstdmt -$level >"$tmp_dir/$tar_file"

	if [ -n "$SUDO_UID" ]; then
		chown -R $SUDO_UID:$SUDO_GID $tmp_dir
	fi

	mv "$tmp_dir/$tar_file" $PWD
}

trap error_cleanup EXIT

tmp_dir=$(mktemp -d)
log_dir=$tmp_dir/$date_str/logs
meta_log_path=$log_dir/meta.log

echo INFO: Using tmp dir $tmp_dir
echo INFO: Test run date $date_str

mkdir -p "$log_dir"


# generate erase file (erase command fails but does end up erasing the flash memory)
cat >"$tmp_dir/erase.jlink" << EOF
ExitOnError 1
r
ExitOnError 0
erase
exit
EOF

# generate flash bootloader file
cat >"$tmp_dir/flash-bootloader.jlink" << EOF
ExitOnError 1
r
loadfile $bl_fw_file
r
go
exit
EOF

errors=0

# erase target
echo INFO: Erase target flash | tee -a "$meta_log_path"
JLinkExe $jlink_options -CommandFile "$tmp_dir/erase.jlink" 2>&1 | tee -a "$log_dir/erase.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed to erase device (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

# wait a bit (device enumeration)
sleep $usb_enum_pause_s

# flash bootloader
echo INFO: Flashing bootloader | tee -a "$meta_log_path"
JLinkExe $jlink_options -CommandFile "$tmp_dir/flash-bootloader.jlink" 2>&1 | tee -a "$log_dir/flash-bootloader.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed to flash bootloader to device (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

# wait a bit (device enumeration)
sleep $usb_enum_pause_s

# upload app through DFU (empty flash)
echo INFO: Uploading application onto empty flash | tee -a "$meta_log_path"
sudo dfu-util -d 1d50:5035,:5036 -R -D "$app_fw_file" 2>&1 | tee -a "$log_dir/dfu-upload-app-empty.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed to upload application to device through DFU (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

# wait a bit (device enumeration)
sleep $usb_enum_pause_s

# upload app through DFU (app bootloader decend)
echo INFO: Application bootloader decend and firmware upload | tee -a "$meta_log_path"
sudo dfu-util -d 1d50:5035,:5036 -R -D "$app_fw_file" 2>&1 | tee -a "$log_dir/dfu-decend-to-bl-then-upload-app.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed to decend into bootloader / upload application to device through DFU (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi


#######################
# archive results
#######################
echo
echo INFO: Finished with $errors errors. | tee -a "$meta_log_path"

pack_results

cleanup

echo INFO: Packed results and cleaned up.

exit $errors

