#!/bin/bash

#set -e
#set -x

script_dir=$(dirname $0)

usb_enum_pause_s=3
jlink_options="-device ATSAME51J18 -if swd -JTAGConf -1,-1 -speed auto"
vid=1d50
pid_bl=5036
pid_app=5035
dfu_util_detach_options="-R -e"


usage()
{
	echo $(basename $0) \[OPTIONS\] BL_FW_FILE BL_DFU_FILE APP_DFU_FILE
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
		*) # unknown option
			POSITIONAL+=("$1") # save it in an array for later
			shift # past argument
 			;;
 	esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters

if [ ${#POSITIONAL[@]} -ne 3 ]; then
	echo "ERROR: $(basename $0) BL_FW_FILE BL_DFU_FILE APP_DFU_FILE"
	exit 1
fi

bl_fw_file=${POSITIONAL[0]}
bl_dfu_file=${POSITIONAL[1]}
app_dfu_file=${POSITIONAL[2]}

if [ ! -f "$bl_fw_file" ]; then
	echo "ERROR: no such file $bl_fw_file"
	exit 1
fi

if [ ! -f "$bl_dfu_file" ]; then
	echo "ERROR: no such file $bl_dfu_file"
	exit 1
fi

if [ ! -f "$app_dfu_file" ]; then
	echo "ERROR: no such file $app_dfu_file"
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


errors=0
number=0
message=

verify_bl_is_running()
{
	message="verify bootloader is running"
	echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
	output=$( lsusb -v -d ${vid}:${pid_bl} 2>&1 | tee -a "$log_dir/${number}-verify-bl-is-running.log" )


	if [ -z "$output" ]; then
		echo "ERROR: failed step \"$message\": could not list device in DFU mode (${vid}:${pid_bl})" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi
}

verify_bl_is_running_from_bank()
{
	local target_bank=$1
	shift

	verify_bl_is_running

	local actual_bank=$( grep Bank "$log_dir/${number}-verify-bl-is-running.log" | sed -E 's/.* Bank ([0-9])/\1/' )

	if [ -z "$actual_bank" ]; then
		echo "ERROR: failed step \"$message\": failed to determine current flash memory bank" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	number=$((number+1))

	message="verify bootloader running from bank $target_bank"
	echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
	if [ $target_bank -ne $actual_bank ]; then
		echo "ERROR: failed step \"$message\": bootloader upload didn't start from bank $target_bank" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi
}

verify_app_is_running()
{
	message="verify the application is running"
	echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
	local output=$( lsusb -v -d ${vid}:${pid_app} 2>&1 | tee -a "$log_dir/${number}-verify-app-is-running.log" )


	if [ -z "$output" ]; then
		echo "ERROR: failed step \"$message\": could not list device in RT mode (${vid}:${pid_app})" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi
}


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



# erase target
message="erase target flash"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
JLinkExe $jlink_options -CommandFile "$tmp_dir/erase.jlink" 2>&1 | tee -a "$log_dir/${number}-erase-chip.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s


message="verify neither bootloader nor application is running"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
output_bl=$( lsusb -v -d ${vid}:${pid_bl} 2>&1 | tee -a "$log_dir/${number}-verify-no-dfu-app-vid-pid-after-erase.log" )
output_app=$( lsusb -v -d ${vid}:${pid_app} 2>&1 | tee -a "$log_dir/${number}-verify-no-dfu-app-vid-pid-after-erase.log" )


if [ -n "$output_bl" ] || [ -n "$output_app" ]; then
	echo "ERROR: failed step \"$message\": found USB device for VID:PID ${vid}:${pid_bl},${pid_app}" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

# echo "INFO: Successfully finished step $number: $message" | tee -a "$meta_log_path"

message="flash bootloader onto empty chip"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
JLinkExe $jlink_options -CommandFile "$tmp_dir/flash-bootloader.jlink" 2>&1 | tee -a "$log_dir/${number}-flash-bootloader.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi


number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_bl_is_running


initial_bank=$( grep Bank "$log_dir/${number}-verify-bl-is-running.log" | sed -E 's/.* Bank ([0-9])/\1/' )

if [ -z "$initial_bank" ]; then
	echo "ERROR: failed step \"$message\": failed to determine initial flash memory bank" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

message="upload application onto empty flash"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$app_dfu_file" 2>&1 | tee -a "$log_dir/${number}-bl-upload-app-onto-empty-flash.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_app_is_running

number=$((number+1))

message="application to bootloader decend"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app} -R -e 2>&1 | tee -a "$log_dir/${number}-app-decend-to-bl.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_bl_is_running_from_bank $initial_bank

number=$((number+1))

message="application upload"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$app_dfu_file" 2>&1 | tee -a "$log_dir/${number}-upload-app.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_app_is_running

number=$((number+1))

message="application to bootloader decend"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app} -R -e 2>&1 | tee -a "$log_dir/${number}-app-decend-to-bl.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_bl_is_running_from_bank $initial_bank

number=$((number+1))


message="bootloader upload onto empty alternate bank"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$bl_dfu_file" 2>&1 | tee -a "$log_dir/${number}-upload-bl.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

# should be bootloader, not app because the flash was empty
verify_bl_is_running

alternate_bank=$( grep Bank "$log_dir/${number}-verify-bl-is-running.log" | sed -E 's/.* Bank ([0-9])/\1/' )

if [ -z "$alternate_bank" ]; then
	echo "ERROR: failed step \"$message\": failed to determine alternate flash memory bank" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))

message="verify bootloader upload switched banks"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
if [ $initial_bank -eq $alternate_bank ]; then
	echo "ERROR: failed step \"$message\": bootloader upload didn't switch banks" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))

message="application upload"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$app_dfu_file" 2>&1 | tee -a "$log_dir/${number}-upload-app.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_app_is_running

number=$((number+1))

message="application to bootloader decend"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app} ${dfu_util_detach_options} 2>&1 | tee -a "$log_dir/${number}-app-decend-to-bl.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_bl_is_running_from_bank $alternate_bank

number=$((number+1))



message="bootloader upload onto the bootloader initially flashed"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$bl_dfu_file" 2>&1 | tee -a "$log_dir/${number}-upload-bl.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

# should be app now
verify_app_is_running


message="application to bootloader decend"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app} ${dfu_util_detach_options} 2>&1 | tee -a "$log_dir/${number}-app-decend-to-bl.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

verify_bl_is_running_from_bank $initial_bank

number=$((number+1))

message="switch back to RT mode"
echo "INFO: Step $number: $message" | tee -a "$meta_log_path"
dfu-util -d ${vid}:${pid_app} -R -e 2>&1 | tee -a "$log_dir/${number}-reset.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
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

