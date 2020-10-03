#!/bin/bash

set -e
#set -x

usage()
{
	echo $(basename $0) GOODCAN TESTCAN
}

#https://stackoverflow.com/questions/192249/how-do-i-parse-command-line-arguments-in-bash
POSITIONAL=()
while [ $# -gt 0 ]; do
	key="$1"

 	case $key in
# 		-e|--extension)
# 		EXTENSION="$2"
# 		shift # past argument
# 		shift # past value
# 		;;
# 		-s|--searchpath)
# 		SEARCHPATH="$2"
# 		shift # past argument
# 		shift # past value
# 		;;
# 		-l|--lib)
# 		LIBPATH="$2"
# 		shift # past argument
# 		shift # past value
# 		;;
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
	echo -n "ERROR: "
	usage
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
		echo Removing $tmp_dir
		rm -rf "$tmp_dir"
	fi
}

trap cleanup EXIT

tmp_dir=$(mktemp -d)
date_str=$(date +"%F_%H%M%S")
log_dir=$tmp_dir/$date_str/logs

echo Using tmp dir $tmp_dir
echo Test run date $date_str

mkdir -p "$log_dir"

cans="$can_good $can_test"

for can in $cans; do
	ip link set down $can || true
	ip link set $can type can bitrate 1000000 dbitrate 5000000 fd on
	ip link set up $can
done

# # clear out any old messages
# sleep 1

# for can in $cans; do
# 	ip link set down $can
# 	ip link set up $can
# done

max_frames=60
errors=0
meta_log_path=$log_dir/meta.log

# run tests
#######################
# good -> test
#######################
echo Sending from good device $can_good

good_to_test_file_test_name=good_to_test_file_test.log
good_to_test_file_test_path=$log_dir/$good_to_test_file_test_name
candump -n $max_frames -t z -L $can_test >$good_to_test_file_test_path &
good_to_test_test_pid=$!

good_to_test_file_good_name=good_to_test_file_good.log
good_to_test_file_good_path=$log_dir/$good_to_test_file_good_name
candump -n $max_frames -t z -L $can_good >$good_to_test_file_good_path &
good_to_test_good_pid=$!

# wait a bit, else we may not get first frame
sleep 2

#cangen -g 1 -m -n $max_frames $can_good
cangen -I 42 -L 8 -D i -g 1 -b -n $max_frames $can_good


sleep 1
kill $good_to_test_test_pid $good_to_test_good_pid 2>/dev/null || true

cat $good_to_test_file_test_path | awk '{print $3}' >$good_to_test_file_test_path.3
cat $good_to_test_file_good_path | awk '{print $3}' >$good_to_test_file_good_path.3

set +e
diff $good_to_test_file_good_path.3 $good_to_test_file_test_path.3 1>/dev/null
if [ $? -ne 0 ]; then
	echo ERROR: good -\> test results differ! | tee -a "$meta_log_path"
	errors=$((errors+1))
fi
set -e

#######################
# test -> good
#######################
echo Sending from test device $can_test

test_to_good_file_test_name=test_to_good_file_test.log
test_to_good_file_test_path=$log_dir/$test_to_good_file_test_name
candump -n $max_frames -t z -L $can_test >$test_to_good_file_test_path &
test_to_good_test_pid=$!

test_to_good_file_good_name=test_to_good_file_good.log
test_to_good_file_good_path=$log_dir/$test_to_good_file_good_name
candump -n $max_frames -t z -L $can_good >$test_to_good_file_good_path &
test_to_good_good_pid=$!

# wait a bit, else we may not get first frame
sleep 2

#cangen -g 1 -m -n $max_frames $can_good
cangen -I 42 -L 8 -D i -g 1 -b -n $max_frames $can_test


sleep 1
kill $test_to_good_test_pid $test_to_good_good_pid 2>/dev/null || true

cat $test_to_good_file_test_path | awk '{print $3}' >$test_to_good_file_test_path.3
cat $test_to_good_file_good_path | awk '{print $3}' >$test_to_good_file_good_path.3

set +e
diff $test_to_good_file_good_path.3 $test_to_good_file_test_path.3 1>/dev/null
if [ $? -ne 0 ]; then
	echo ERROR: test -\> good results differ! | tee -a "$meta_log_path"
	errors=$((errors+1))
fi
set -e


#######################
# archive results
#######################

tar_file=testresult-${date_str}.tar.xz
tar -C "$tmp_dir" -c . | pixz >"$tmp_dir/$tar_file"

if [ -n "$SUDO_UID" ]; then
	chown -R $SUDO_UID:$SUDO_GID $tmp_dir
fi

mv "$tmp_dir/$tar_file" $PWD

cleanup

echo Finished with $errors errors.
exit $errors











