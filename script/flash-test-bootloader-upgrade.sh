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

cmdl_args="PREV_BL_FW_HEX_FILE PREV_APP_BL_HEX_FILE UPGRADE_BL_DFU_FILE CURR_BL_DFU_FILE CURR_APP_DFU_FILE"

usage()
{
	echo $(basename $0) \[OPTIONS\] $cmdl_args
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

if [ ${#POSITIONAL[@]} -ne 5 ]; then
	echo "ERROR: $(basename $0) $cmdl_args"
	exit 1
fi

prev_bl_fw_file=${POSITIONAL[0]}
prev_app_fw_file=${POSITIONAL[1]}
upgrade_bl_dfu_file=${POSITIONAL[2]}
curr_bl_dfu_file=${POSITIONAL[3]}
curr_app_dfu_file=${POSITIONAL[4]}

if [ ! -f "$prev_bl_fw_file" ]; then
	echo "ERROR: no such file $prev_bl_fw_file"
	exit 1
fi

if [ ! -f "$prev_app_fw_file" ]; then
	echo "ERROR: no such file $prev_app_fw_file"
	exit 1
fi

if [ ! -f "$curr_bl_dfu_file" ]; then
	echo "ERROR: no such file $curr_bl_dfu_file"
	exit 1
fi

if [ ! -f "$curr_app_dfu_file" ]; then
	echo "ERROR: no such file $curr_app_dfu_file"
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
	local tar_file=flash-test-bootloader-upgrade-result-${date_str}.tar.zst

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

verify_bootloader_is_running()
{
	local setting_name=$1
	local setting_log_dir=$log_dir/$setting_name

	message="verify bootloader is running"
	echo "INFO[${setting_name}]: Step $number: $message" | tee -a "$meta_log_path"
	output=$( lsusb -v -d ${vid}:${pid_bl} 2>&1 | tee -a "${setting_log_dir}/${number}-verify-bootloader-is-running.log" )


	if [ -z "$output" ]; then
		echo "ERROR[${setting_name}]: failed step \"$message\": could not list device in DFU mode (${vid}:${pid_bl})" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi
}

verify_app_is_running()
{
	local setting_name=$1
	local setting_log_dir=$log_dir/$setting_name

	message="verify the application is running"
	echo "INFO[${setting_name}]: Step $number: $message" | tee -a "$meta_log_path"
	local output=$( lsusb -v -d ${vid}:${pid_app} 2>&1 | tee -a "$setting_log_dir/${number}-verify-app-is-running.log" )


	if [ -z "$output" ]; then
		echo "ERROR[${setting_name}]: failed step \"$message\": could not list device in RT mode (${vid}:${pid_app})" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi
}

app_descend_to_bootloader()
{
	local setting_name=$1
	local setting_log_dir=$log_dir/$setting_name

	message="application to bootloader decend"
	echo "INFO[${setting_name}]: Step $number: $message" | tee -a "$meta_log_path"
	dfu-util -d ${vid}:${pid_app} -R -e 2>&1 | tee -a "$setting_log_dir/${number}-app-decend-to-bl.log"
	exit_code=${PIPESTATUS[0]}

	if [ $exit_code -ne 0 ]; then
		echo "ERROR[${setting_name}]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s
}



# generate erase file (erase command fails but does end up erasing the flash memory)
cat >"$tmp_dir/erase.jlink" << EOF
ExitOnError 1
r
ExitOnError 0
erase
exit
EOF

# generate flash previous bootloader file
cat >"$tmp_dir/flash-bootloader.jlink" << EOF
ExitOnError 1
r
loadfile $prev_bl_fw_file
r
go
exit
EOF

# generate flash previous app file
cat >"$tmp_dir/flash-app.jlink" << EOF
ExitOnError 1
r
loadfile $prev_app_fw_file
r
go
exit
EOF

# generate junk file
dd if=/dev/urandom "of=$tmp_dir/junk.bin" bs=32K count=1

# generate flash junk file
cat >"$tmp_dir/flash-junk.jlink" << EOF
ExitOnError 1
r
loadfile $tmp_dir/junk.bin,0x0
r
go
exit
EOF


function app_update_test()
{
	local setting_name=$1
	local setting_log_dir=$log_dir/$setting_name

	message="application upload onto bootloader only device"
	echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
	dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$curr_app_dfu_file" 2>&1 | tee -a "$setting_log_dir/${number}-upload-app-onto-bl-only.log"
	exit_code=${PIPESTATUS[0]}

	if [ $exit_code -ne 0 ]; then
		echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s

	number=$((number+1))

	verify_app_is_running $setting_name
	number=$((number+1))


	# update app again (self update)
	app_descend_to_bootloader $setting_name
	number=$((number+1))

	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s

	verify_bootloader_is_running $setting_name
	number=$((number+1))

	message="application upload (app update)"
	echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
	dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$curr_app_dfu_file" 2>&1 | tee -a "$setting_log_dir/${number}-upload-app-update.log"
	exit_code=${PIPESTATUS[0]}

	if [ $exit_code -ne 0 ]; then
		echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s

	number=$((number+1))

	verify_app_is_running $setting_name
	number=$((number+1))
}


function bootloader_upgrade_test()
{
	local setting_name=$1
	local setting_log_dir=$log_dir/$setting_name


	verify_bootloader_is_running $setting_name
	number=$((number+1))

	## Bootloader upgrade to upgrade
	message="bootloader upload through DFU from prev default to upgrade version"
	echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
	dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$upgrade_bl_dfu_file" 2>&1 | tee -a "$setting_log_dir/${number}-upload-bl-prev-default-to-upgrade.log"
	exit_code=${PIPESTATUS[0]}

	if [ $exit_code -ne 0 ]; then
		echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s

	number=$((number+1))

	verify_bootloader_is_running $setting_name
	number=$((number+1))

	# -> this part fails for update bootloader, gets stuck in tud_task <-

	# app_update_test  $setting_name
	# number=$((number+1))

	# ## prep self-update
	# app_descend_to_bootloader $setting_name
	# number=$((number+1))

	# verify_bootloader_is_running $setting_name
	# number=$((number+1))

	## Bootloader update to default
	message="bootloader upload through DFU from upgrade to default version"
	echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
	dfu-util -d ${vid}:${pid_app},:${pid_bl} -R -D "$curr_bl_dfu_file" 2>&1 | tee -a "$setting_log_dir/${number}-upload-bl-upgrade-to-curr-default.log"
	exit_code=${PIPESTATUS[0]}

	if [ $exit_code -ne 0 ]; then
		echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s

	number=$((number+1))

	verify_bootloader_is_running $setting_name
	number=$((number+1))

	app_update_test  $setting_name
	number=$((number+1))
}

function from_naked_device()
{
	local setting_name=$1
	local setting_log_dir=$log_dir/$setting_name

	message="verify neither bootloader nor application is running"
	echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
	output_bl=$( lsusb -v -d ${vid}:${pid_bl} 2>&1 | tee -a "$setting_log_dir/${number}-verify-no-dfu-app-vid-pid-after-erase.log" )
	output_app=$( lsusb -v -d ${vid}:${pid_app} 2>&1 | tee -a "$setting_log_dir/${number}-verify-no-dfu-app-vid-pid-after-erase.log" )


	if [ -n "$output_bl" ] || [ -n "$output_app" ]; then
		echo "ERROR[$setting_name]: failed step \"$message\": found USB device for VID:PID ${vid}:${pid_bl},${pid_app}" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi


	message="flash prev bootloader onto chip"
	echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
	JLinkExe $jlink_options -CommandFile "$tmp_dir/flash-bootloader.jlink" 2>&1 | tee -a "$setting_log_dir/${number}-flash-prev-bootloader.log"
	exit_code=${PIPESTATUS[0]}

	if [ $exit_code -ne 0 ]; then
		echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi


	number=$((number+1))
	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s

	verify_bl_is_running $setting_name

	message="flash prev app onto chip"
	echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
	JLinkExe $jlink_options -CommandFile "$tmp_dir/flash-app.jlink" 2>&1 | tee -a "$setting_log_dir/${number}-flash-prev-app.log"
	exit_code=${PIPESTATUS[0]}

	if [ $exit_code -ne 0 ]; then
		echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi


	number=$((number+1))
	# wait a bit (device enumeration)
	sleep $usb_enum_pause_s


	verify_app_is_running $setting_name
	number=$((number+1))

	app_descend_to_bootloader $setting_name
	number=$((number+1))

	bootloader_upgrade_test $setting_name


}


##################
### from blank ###
##################
setting_name="from_blank"
setting_log_dir=$log_dir/$setting_name

echo
echo INFO: Setting $setting_name

mkdir -p "$setting_log_dir"

number=1

# erase target
message="erase target flash"
echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
JLinkExe $jlink_options -CommandFile "$tmp_dir/erase.jlink" 2>&1 | tee -a "$setting_log_dir/${number}-erase-chip.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

from_naked_device $setting_name

#################
### from junk ###
#################

setting_name="from_junk"
setting_log_dir=$log_dir/$setting_name

echo
echo INFO: Setting $setting_name

mkdir -p "$setting_log_dir"

number=1

# erase target
message="flash junk"
echo "INFO[$setting_name]: Step $number: $message" | tee -a "$meta_log_path"
JLinkExe $jlink_options -CommandFile "$tmp_dir/flash-junk.jlink" 2>&1 | tee -a "$setting_log_dir/${number}-flash-junk.log"
exit_code=${PIPESTATUS[0]}

if [ $exit_code -ne 0 ]; then
	echo "ERROR[$setting_name]: failed step \"$message\" (exit code $exit_code)" | tee -a "$meta_log_path"
	errors=$((errors+1))
fi

number=$((number+1))
# wait a bit (device enumeration)
sleep $usb_enum_pause_s

from_naked_device $setting_name

#######################
# archive results
#######################
echo
echo INFO: Finished with $errors errors. | tee -a "$meta_log_path"

pack_results

cleanup

echo INFO: Packed results and cleaned up.

exit $errors

