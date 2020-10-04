#!/bin/bash

set -e
#set -x

script_dir=$(dirname $0)

usage()
{
	echo $(basename $0) GOODCAN TESTCAN
	echo
	echo "   -s, --seconds SECONDS    limit test runs to SECONDS"
	echo "   --no-init                don't initialize can devices"
	echo
}

seconds=300
init=1

#https://stackoverflow.com/questions/192249/how-do-i-parse-command-line-arguments-in-bash
POSITIONAL=()
while [ $# -gt 0 ]; do
	key="$1"

 	case $key in
 		-s|--seconds)
 			seconds="$2"
 			shift # past argument
 			shift # past value
 			;;
		-h|--help)
			usage
			exit 0
			;;
		--no-init)
			init=0
			shift # past argument
			;;
# 		--default)
# 		DEFAULT=YES
# 		shift # past argument
# 		;;
		*)    # unknown option
			POSITIONAL+=("$1") # save it in an array for later
			shift # past argument
 			;;
 	esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters


if [ ${#POSITIONAL[@]} -ne 2 ]; then
	echo "ERROR: $(basename $0) GOODCAN TESTCAN"
	exit 1
fi


if [ 0 -ne $(id -u) ]; then
	echo ERROR: $(basename $0) must be run as root!
	exit 1
fi


can_good=${POSITIONAL[0]}
can_test=${POSITIONAL[1]}

if [ -z "$(ip link show $can_good 2>/dev/null)" ]; then
	echo ERROR: $can_good not found!
	exit 1
fi

if [ -z "$(ip link show $can_test 2>/dev/null)" ]; then
	echo ERROR: $can_test not found!
	exit 1
fi


tmp_dir=

cleanup()
{
	if [ ! -z "$tmp_dir" ] && [ -d "$tmp_dir" ]; then
		echo INFO: Removing $tmp_dir
		rm -rf "$tmp_dir"
	fi
}

error_cleanup()
{
	local rc=$?
	if [ 0 -ne $rc ]; then
		echo ERROR: command failed with $rc
	fi

	cleanup
}

trap error_cleanup EXIT

tmp_dir=$(mktemp -d)
date_str=$(date +"%F_%H%M%S")
log_dir=$tmp_dir/$date_str/logs
meta_log_path=$log_dir/meta.log

echo INFO: Using tmp dir $tmp_dir
echo INFO: Test run date $date_str

mkdir -p "$log_dir"

echo INFO: Run tests for $seconds seconds | tee -a "$meta_log_path"


cans="$can_good $can_test"
if [ $init -ne 0 ];then
	echo INFO: Initialize devices to nominal 1MBit/s data 5MBit/s | tee -a "$meta_log_path"
	for can in $cans; do
		ip link set down $can || true
		ip link set $can type can bitrate 1000000 dbitrate 5000000 fd on
		ip link set up $can
	done
fi

# # clear out any old messages
# sleep 1

# for can in $cans; do
# 	ip link set down $can
# 	ip link set up $can
# done


max_frames=$((seconds*1000))
errors=0

#-I 42 -L 8 -D i -g 1 -b -n $max_frames
can_gen_flags="-e -I r -L r -D r -g 1 -b -n $max_frames"

same_messages()
{
	local rc=0

	good_path=$1
	test_path=$2

	cat "$good_path" | awk '{print $3}' >"$good_path.3"
	cat "$test_path" | awk '{print $3}' >"$test_path.3"

	diff "$good_path.3" "$test_path.3" 1>/dev/null
	if [ $? -ne 0 ]; then
		rc=1
	fi

	return $rc
}


# run tests
#######################
# good -> test
#######################
echo INFO: Sending from good device $can_good | tee -a "$meta_log_path"

good_to_test_file_test_name=good_to_test_file_test.log
good_to_test_file_test_path=$log_dir/$good_to_test_file_test_name
candump -n $max_frames -H -t z -L $can_test >$good_to_test_file_test_path &
good_to_test_test_pid=$!

good_to_test_file_good_name=good_to_test_file_good.log
good_to_test_file_good_path=$log_dir/$good_to_test_file_good_name
candump -n $max_frames -H -t z -L $can_good >$good_to_test_file_good_path &
good_to_test_good_pid=$!

# wait a bit, else we may not get first frame
sleep 2

cangen $can_gen_flags $can_good


sleep 1
kill $good_to_test_test_pid $good_to_test_good_pid 2>/dev/null || true

set +e
same_messages "$good_to_test_file_good_path" "$good_to_test_file_test_path"
if [ $? -ne 0 ]; then
	echo ERROR: good -\> test messages DIFFER! | tee -a "$meta_log_path"
	errors=$((errors+1))
else
	echo INFO: good -\> test messages OK! | tee -a "$meta_log_path"
fi

mono_out=$("$script_dir/ensure-strict-mono.py" "$good_to_test_file_test_path" 2>&1)
if [ $? -ne 0 ]; then
	echo ERROR: good -\> test timestamps NOT mono! | tee -a "$meta_log_path"
	echo ERROR: $mono_out | tee -a "$meta_log_path"
	errors=$((errors+1))
else
	echo INFO: good -\> test timestamps mono OK! | tee -a "$meta_log_path"
fi
set -e

#######################
# test -> good
#######################
echo INFO: Sending from test device $can_test | tee -a "$meta_log_path"

test_to_good_file_test_name=test_to_good_file_test.log
test_to_good_file_test_path=$log_dir/$test_to_good_file_test_name
candump -n $max_frames -H -t z -L $can_test >$test_to_good_file_test_path &
test_to_good_test_pid=$!

test_to_good_file_good_name=test_to_good_file_good.log
test_to_good_file_good_path=$log_dir/$test_to_good_file_good_name
candump -n $max_frames -H -t z -L $can_good >$test_to_good_file_good_path &
test_to_good_good_pid=$!

# wait a bit, else we may not get first frame
sleep 2

cangen $can_gen_flags $can_test


sleep 1
kill $test_to_good_test_pid $test_to_good_good_pid 2>/dev/null || true


set +e
same_messages "$test_to_good_file_good_path" "$test_to_good_file_test_path"
if [ $? -ne 0 ]; then
	echo ERROR: test -\> good messages DIFFER! | tee -a "$meta_log_path"
	errors=$((errors+1))
else
	echo INFO: test -\> good messages OK! | tee -a "$meta_log_path"
fi

mono_out=$("$script_dir/ensure-strict-mono.py" "$test_to_good_file_test_path" 2>&1)
if [ $? -ne 0 ]; then
	echo ERROR: test -\> good timestamps NOT mono! | tee -a "$meta_log_path"
	echo ERROR: $mono_out | tee -a "$meta_log_path"
	errors=$((errors+1))
else
	echo INFO: test -\> good timestamps mono OK! | tee -a "$meta_log_path"
fi
set -e

#######################
# archive results
#######################
tar_file=testresult-${date_str}.tar.xz

echo INFO: creating archive $tar_file
tar -C "$tmp_dir" -c . | pixz -9 >"$tmp_dir/$tar_file"

if [ -n "$SUDO_UID" ]; then
	chown -R $SUDO_UID:$SUDO_GID $tmp_dir
fi

mv "$tmp_dir/$tar_file" $PWD

cleanup

echo INFO: Finished with $errors errors.
exit $errors











