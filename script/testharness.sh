#!/bin/bash

set -e
#set -x

script_dir=$(dirname $0)

default_seconds=300
default_level=3
default_data_bitrate=8000000
seconds=${default_seconds}
test_jitter=1
test_error_recovery=1
level=${default_level}
sort=1
data_bitrate=${default_data_bitrate}

usage()
{
	echo $(basename $0) \[OPTIONS\] GOODCAN TESTCAN
	echo
	echo "   -s, --seconds SECONDS    limit test runs to SECONDS (default ${default_seconds})"
	echo "   --no-jitter              don't run timestamp jitter test"
	echo "   --no-error-recovery      don't run error recovery test"
	echo "   --comp-level LEVEL       compress with LEVEL (default ${default_level})"
	echo "   --no-sort                don't sort output by timestamp prior to comparision"
	echo "   --data-bitrate           set bitrate for data phase (default ${default_data_bitrate})"
	echo
}



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
		--no-jitter)
			test_jitter=0
			shift # past argument
			;;
		--no-error-recovery)
			test_error_recovery=0
			shift # past argument
			;;
		--no-sort)
			sort=0
			shift # past argument
			;;
		--comp-level)
			level=$2
			shift # past argument
			shift # past value
			;;
		--data-bitrate)
			data_bitrate=$2
			shift # past argument
			shift # past value
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
	local tar_file=testresult-${date_str}.tar.zst

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

echo INFO: Run tests for $seconds seconds | tee -a "$meta_log_path"


cans="$can_good $can_test"


errors=0
startup_wait_s=2
candump_wait_s=3
frame_len=r
alias candump='stdbuf -oL candump'
candump_options="-t a -L"
sort_options="-k 1.2,1.18 -s"

same_messages()
{
	#local rc=0

	local good_path=$1
	local test_path=$2
	local column=$3

	cat "$good_path" | awk "{print \$$column;}" >"$good_path.msgs"
	cat "$test_path" | awk "{print \$$column;}" >"$test_path.msgs"

	diff "$good_path.msgs" "$test_path.msgs" 1>/dev/null
	# if [ $? -ne 0 ]; then
	# 	rc=1
	# fi

	# return $rc
	return $?
}


# run tests

################
# error recovery
################


if [ $test_error_recovery -ne 0 ]; then
	error_recovery_error_passive_log_good_can_tx_path=$log_dir/error_recovery_error_passive_log_good_can_tx.log
	error_recovery_error_passive_log_test_can_rx_path=$log_dir/error_recovery_error_passive_log_test_can_rx.log
	error_recovery_error_passive_log_good_can_rx_path=$log_dir/error_recovery_error_passive_log_good_can_tx.log
	error_recovery_error_passive_log_test_can_tx_path=$log_dir/error_recovery_error_passive_log_test_can_tx.log
	error_recovery_error_passive_stats_test_can_path=$log_dir/error_recovery_error_passive_stats_test_can.log


	error_recovery_bus_off_log_good_can_tx_path=$log_dir/error_recovery_bus_off_log_good_can_tx.log
	error_recovery_bus_off_log_test_can_rx_path=$log_dir/error_recovery_bus_off_log_test_can_rx.log
	error_recovery_bus_off_log_good_can_rx_path=$log_dir/error_recovery_bus_off_log_good_can_rx.log
	error_recovery_bus_off_log_test_can_tx_path=$log_dir/error_recovery_bus_off_log_test_can_tx.log
	error_recovery_bus_off_stats_test_can_path=$log_dir/error_recovery_bus_off_stats_test_can.log

	error_recovery_good_can_bitrate=500000
	error_recovery_test_can_bitrate=1000000
	error_recovery_tx_frames=10
	error_recovery_acceptable_rx_frames=$error_recovery_tx_frames




	################
	# GOOD -> TEST
	################

	for can in $cans; do
		ip link set down $can || true
	done

	ip link set $can_good type can bitrate $error_recovery_good_can_bitrate fd off
	ip link set $can_test type can bitrate $error_recovery_test_can_bitrate fd off

	ip link set up $can_good
	ip link set up $can_test

	echo "INFO: Sending frames from $can_good (bitrate $error_recovery_good_can_bitrate), expecting $can_test (bitrate $error_recovery_test_can_bitrate) to go into error-passive" | tee -a "$meta_log_path"
	cangen $can_good -L 8 -D ffffffffffffffff -I 7ff -n 10

	ip -details -statistics link show $test_can >$error_recovery_error_passive_stats_test_can_path 2>&1

	error_passive=`grep "can state ERROR-PASSIVE" $error_recovery_error_passive_stats_test_can_path`
	if [ -n "$error_passive" ]; then
		echo INFO: $can_test in error-passive state, OK! | tee -a "$meta_log_path"
	else
		echo ERROR: $can_test not in error-passive state! | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	echo "INFO: Bringing $can_good to $error_recovery_test_can_bitrate bps, expecting rx/tx to work in both directions." | tee -a "$meta_log_path"
	ip link set down $can_good
	ip link set $can_good type can bitrate $error_recovery_test_can_bitrate fd off
	ip link set up $can_good

	candump $candump_options $can_good >$error_recovery_error_passive_log_good_can_tx_path &
	candump_good_pid=$!

	candump $candump_options $can_test >$error_recovery_error_passive_log_test_can_rx_path &
	candump_test_pid=$!

	echo "INFO: good -> test" | tee -a "$meta_log_path"
	cangen $can_good -L 8 -D i -I i -n $error_recovery_tx_frames

	sleep $candump_wait_s

	kill $candump_good_pid 2>/dev/null || true
	kill $candump_test_pid 2>/dev/null || true

	set +e

	lines=$(cat "$error_recovery_error_passive_log_good_can_tx_path" | wc -l)
	if [ $lines -ne $error_recovery_acceptable_rx_frames ]; then
		echo ERROR: GOOD log file messages $lines/$error_recovery_tx_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: GOOD log file $lines/$error_recovery_tx_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$error_recovery_error_passive_log_test_can_rx_path" | wc -l)
	if [ $lines -ne $error_recovery_tx_frames ]; then
		echo ERROR: TEST log file messages $lines/$error_recovery_tx_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: TEST log file $lines/$error_recovery_tx_frames messages, error-passive recovery OK! | tee -a "$meta_log_path"
	fi

	set -e

	echo "INFO: test -> good" | tee -a "$meta_log_path"

	candump $candump_options $can_good >$error_recovery_error_passive_log_good_can_rx_path &
	candump_good_pid=$!

	candump $candump_options $can_test >$error_recovery_error_passive_log_test_can_tx_path &
	candump_test_pid=$!

	cangen $can_test -L 8 -D i -I i -n $error_recovery_tx_frames

	sleep $candump_wait_s

	kill $candump_good_pid 2>/dev/null || true
	kill $candump_test_pid 2>/dev/null || true

	set +e

	lines=$(cat "$error_recovery_error_passive_log_good_can_rx_path" | wc -l)
	if [ $lines -ne $error_recovery_tx_frames ]; then
		echo ERROR: GOOD log file messages $lines/$error_recovery_tx_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: GOOD log file $lines/$error_recovery_tx_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$error_recovery_error_passive_log_test_can_tx_path" | wc -l)
	if [ $lines -ne $error_recovery_tx_frames ]; then
		echo ERROR: TEST log file messages $lines/$error_recovery_tx_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: TEST log file $lines/$error_recovery_tx_frames messages, error-passive recovery OK! | tee -a "$meta_log_path"
	fi

	set -e

	################
	# TEST -> GOOD
	################

	for can in $cans; do
		ip link set down $can || true
	done


	ip link set $can_good type can bitrate $error_recovery_good_can_bitrate fd off
	ip link set $can_test type can bitrate $error_recovery_test_can_bitrate fd off

	ip link set up $can_good
	ip link set up $can_test

	echo "INFO: Sending frames from $can_test (bitrate $error_recovery_test_can_bitrate), expecting $can_test to go into bus-off" | tee -a "$meta_log_path"
	cangen $can_test -L 8 -D ffffffffffffffff -I 7ff -n 10

	ip -details -statistics link show $test_can >$error_recovery_bus_off_stats_test_can_path 2>&1

	set +e

	bus_off=`grep "can state BUS-OFF" $error_recovery_bus_off_stats_test_can_path`
	if [ -n "$bus_off" ]; then
		echo INFO: $can_test in bus-off state, OK! | tee -a "$meta_log_path"
	else
		echo ERROR: $can_test not in bus-off state! | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	set -e

	echo "INFO: Taking down both devices and bringing them to $error_recovery_test_can_bitrate bps, expecting rx/tx to work in both directions." | tee -a "$meta_log_path"
	for can in $cans; do
		ip link set down $can || true
	done

	ip link set $can_good type can bitrate $error_recovery_test_can_bitrate fd off
	ip link set $can_test type can bitrate $error_recovery_test_can_bitrate fd off

	ip link set up $can_good
	ip link set up $can_test

	candump $candump_options $can_good >$error_recovery_bus_off_log_good_can_rx_path &
	candump_good_pid=$!

	candump $candump_options $can_test >$error_recovery_bus_off_log_test_can_tx_path &
	candump_test_pid=$!

	echo "INFO: test -> good" | tee -a "$meta_log_path"
	cangen $can_test -L 8 -D i -I i -n $error_recovery_tx_frames

	sleep $candump_wait_s

	kill $candump_good_pid 2>/dev/null || true
	kill $candump_test_pid 2>/dev/null || true

	set +e

	lines=$(cat "$error_recovery_bus_off_log_test_can_tx_path" | wc -l)
	if [ $lines -ne $error_recovery_tx_frames ]; then
		echo "ERROR: TEST log file contains $lines/$error_recovery_tx_frames!" | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: TEST log file contains $lines/$error_recovery_tx_frames messages, bus-off recovery OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$error_recovery_error_passive_log_test_can_rx_path" | wc -l)
	if [ $lines -ne $error_recovery_tx_frames ]; then
		echo ERROR: GOOD log file $lines/$error_recovery_tx_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: GOOD log file $lines/$error_recovery_tx_frames messages OK! | tee -a "$meta_log_path"
	fi

	set -e

	candump $candump_options $can_good >$error_recovery_bus_off_log_good_can_tx_path &
	candump_good_pid=$!

	candump $candump_options $can_test >$error_recovery_bus_off_log_test_can_rx_path &
	candump_test_pid=$!

	echo "INFO: good -> test" | tee -a "$meta_log_path"
	cangen $can_good -L 8 -D i -I i -n $error_recovery_tx_frames

	sleep $candump_wait_s

	kill $candump_good_pid 2>/dev/null || true
	kill $candump_test_pid 2>/dev/null || true

	set +e

	lines=$(cat "$error_recovery_bus_off_log_good_can_tx_path" | wc -l)
	if [ $lines -ne $error_recovery_tx_frames ]; then
		echo ERROR: GOOD log file messages $lines/$error_recovery_tx_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: GOOD log file $lines/$error_recovery_tx_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$error_recovery_bus_off_log_test_can_rx_path" | wc -l)
	if [ $lines -ne $error_recovery_tx_frames ]; then
		echo ERROR: TEST log file messages $lines/$error_recovery_tx_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO: TEST log file $lines/$error_recovery_tx_frames messages, bus-off recovery OK! | tee -a "$meta_log_path"
	fi

	set -e
else
	echo INFO: not running error recovery tests. | tee -a "$meta_log_path"
fi


settings=(slow fast)
nominal_bitrates=(500000 1000000)
data_bitrates=(500000 $data_bitrate)



function one_each()
{
	local setting_name=$1
	local setting_nominal_bitrate=$2
	local setting_data_bitrate=$3

	echo
	echo "INFO: Setting $setting_name ($setting_nominal_bitrate/$setting_data_bitrate bps)"

	max_frames=$((seconds*$setting_nominal_bitrate/200))
	echo INFO[$setting_name]: max frames $max_frames

	#-I 42 -L 8 -D i -g 1 -b -n $max_frames
	local single_sender_can_gen_flags="-e -I r -L $frame_len -D i -g 0 -p 1 -b -n $max_frames"


	shift
	shift
	shift

	local lines=0
	local setting_log_dir=$log_dir/$setting_name

	mkdir -p "$setting_log_dir"

	echo INFO[$setting_name]: Configure CANs for nominal $setting_nominal_bitrate bps, data $setting_data_bitrate bps | tee -a "$meta_log_path"

	for can in $cans; do
		ip link set down $can || true
		ip link set $can type can bitrate $setting_nominal_bitrate dbitrate ${setting_data_bitrate} fd on
	done


	#########################
	# error frame generation
	#########################
	ip link set up $can_test

	local no_tx_ack_log_cangen_name=test_no_tx_ack_cangen.log
	local no_tx_ack_log_cangen_path=$setting_log_dir/$no_tx_ack_log_cangen_name
	local no_tx_ack_log_candump_name=test_no_tx_ack_candump.log
	local no_tx_ack_log_candump_path=$setting_log_dir/$no_tx_ack_log_candump_name
	local tx_ack_log_candump_name=test_tx_ack_candump.log
	local tx_ack_log_candump_path=$setting_log_dir/$tx_ack_log_candump_name


	echo INFO[$setting_name]: Error frame generation test \(no tx ack\) on $can_test | tee -a "$meta_log_path"
	candump $can_test,#ffffffff -e >$no_tx_ack_log_candump_path &
	local candump_pid=$!

	echo INFO[$setting_name]: Sending CAN frames and expect error indicating no one else is on the bus | tee -a "$meta_log_path"
	# generate some frames to exceed queue capacity
	cangen $can_test -b -n 100 >$no_tx_ack_log_cangen_path 2>&1 || true

	# should fail with "write: No buffer space available"
	local exhausted_send_queue=`grep "write: No buffer space available" $no_tx_ack_log_cangen_path`
	if [ -n "$exhausted_send_queue" ]; then
		echo INFO[$setting_name]: cangen stopped on its own due to full queue OK! | tee -a "$meta_log_path"
	else
		echo ERROR[$setting_name]: expected cangen to exceed send queue capacity of $can_test! | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	kill $candump_pid 2>/dev/null || true

	local no_tx_ack=`grep no-acknowledgement-on-tx $no_tx_ack_log_candump_path`
	if [ -n "$no_tx_ack" ]; then
		echo INFO[$setting_name]: found expected string 'no-acknowledgement-on-tx' in $no_tx_ack_log_candump_path OK! | tee -a "$meta_log_path"
	else
		echo ERROR[$setting_name]: could not find expected string 'no-acknowledgement-on-tx' in $no_tx_ack_log_candump_path! | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi

	candump $candump_options $can_test >$tx_ack_log_candump_path &
	candump_pid=$!

	echo INFO[$setting_name]: Bringing up $can_good to check that queued frames for $can_test get sent | tee -a "$meta_log_path"
	# bring the good CAN online to have ack
	ip link set up $can_good

	sleep $candump_wait_s

	kill $candump_pid 2>/dev/null || true

	local have_can_frames=`grep $can_test $tx_ack_log_candump_path | wc -l`
	if [ $have_can_frames -gt 0 ]; then
		echo INFO[$setting_name]: With second node $can_good on CAN, frames queued in $can_test got sent OK! | tee -a "$meta_log_path"
	else
		echo ERROR[$setting_name]: no frames sent despite both $can_test and $can_good being online! | tee -a "$meta_log_path"
		errors=$((errors+1))
	fi


	#######################
	# good -> test
	#######################
	echo
	echo INFO[$setting_name]: Single sender good -\> test | tee -a "$meta_log_path"
	echo INFO[$setting_name]: Sending from good device $can_good | tee -a "$meta_log_path"

	local good_to_test_file_test_name=good_to_test_file_test.log
	local good_to_test_file_test_path=$setting_log_dir/$good_to_test_file_test_name
	candump -n $max_frames $candump_options -H $can_test >$good_to_test_file_test_path &
	local good_to_test_test_pid=$!

	good_to_test_file_good_name=good_to_test_file_good.log
	good_to_test_file_good_path=$setting_log_dir/$good_to_test_file_good_name
	# PCAN USB-FD doesn't support hw tx timestamps (always zero)
	candump -n $max_frames $candump_options $can_good >$good_to_test_file_good_path &
	local good_to_test_good_pid=$!

	# wait a bit, else we may not get first frame
	sleep $startup_wait_s

	cangen $single_sender_can_gen_flags $can_good

	# brittle!
	sleep $candump_wait_s
	kill $good_to_test_test_pid $good_to_test_good_pid 2>/dev/null || true

	set +e

	if [ $sort -ne 0 ]; then
		env LC_ALL=C sort $sort_options "$good_to_test_file_good_path" >"$good_to_test_file_good_path.sorted"
		env LC_ALL=C sort $sort_options "$good_to_test_file_test_path" >"$good_to_test_file_test_path.sorted"
		good_to_test_file_good_path="$good_to_test_file_good_path.sorted"
		good_to_test_file_test_path="$good_to_test_file_test_path.sorted"
	fi

	lines=$(cat "$good_to_test_file_good_path" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: good -\> test GOOD log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test GOOD log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$good_to_test_file_test_path" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: good -\> test TEST log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test TEST log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	same_messages "$good_to_test_file_good_path" "$good_to_test_file_test_path" 3
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: good -\> test messages DIFFER! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test messages OK! | tee -a "$meta_log_path"
	fi

	local mono_out=$("$script_dir/mono-check.py" "$good_to_test_file_test_path" 2>&1)
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: good -\> test rx timestamps NOT mono! | tee -a "$meta_log_path"
		echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test rx timestamps mono OK! | tee -a "$meta_log_path"
	fi

	local non_zero_time_stamp=$(cat "$good_to_test_file_test_path" | awk '{print $1;}' | grep -v '(0000000000.000000)')
	if [ -z "$non_zero_time_stamp" ]; then
		echo ERROR[$setting_name]: good -\> test TEST log file \(RX\) has ONLY zero time stamps! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test TEST log file \(RX\) has found non-zero time stamp, OK! | tee -a "$meta_log_path"
	fi

	set -e

	#######################
	# test -> good
	#######################
	echo
	echo INFO[$setting_name]: Single sender test -\> good | tee -a "$meta_log_path"
	echo INFO[$setting_name]: Sending from test device $can_test | tee -a "$meta_log_path"

	local test_to_good_file_test_name=test_to_good_file_test.log
	local test_to_good_file_test_path=$setting_log_dir/$test_to_good_file_test_name
	candump -n $max_frames $candump_options -H $can_test >$test_to_good_file_test_path &
	local test_to_good_test_pid=$!

	local test_to_good_file_good_name=test_to_good_file_good.log
	local test_to_good_file_good_path=$setting_log_dir/$test_to_good_file_good_name
	candump -n $max_frames $candump_options -H $can_good >$test_to_good_file_good_path &
	local test_to_good_good_pid=$!

	# wait a bit, else we may not get first frame
	sleep $startup_wait_s

	cangen $single_sender_can_gen_flags $can_test

	# brittle!
	sleep $candump_wait_s
	kill $test_to_good_test_pid $test_to_good_good_pid 2>/dev/null || true


	set +e

	if [ $sort -ne 0 ]; then
		env LC_ALL=C sort $sort_options "$test_to_good_file_good_path" >"$test_to_good_file_good_path.sorted"
		env LC_ALL=C sort $sort_options "$test_to_good_file_test_path" >"$test_to_good_file_test_path.sorted"
		test_to_good_file_good_path="$test_to_good_file_good_path.sorted"
		test_to_good_file_test_path="$test_to_good_file_test_path.sorted"
	fi

	lines=$(cat "$test_to_good_file_good_path" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: test -\> good GOOD log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> good GOOD log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$test_to_good_file_test_path" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: test -\> good TEST log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> good TEST log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	same_messages "$test_to_good_file_good_path" "$test_to_good_file_test_path" 3
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: test -\> good messages DIFFER! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> good messages OK! | tee -a "$meta_log_path"
	fi

	mono_out=$("$script_dir/mono-check.py" "$test_to_good_file_test_path" 2>&1)
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: test -\> good tx timestamps NOT mono! | tee -a "$meta_log_path"
		echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> good tx timestamps mono OK! | tee -a "$meta_log_path"
	fi

	non_zero_time_stamp=$(cat "$test_to_good_file_test_path" | awk '{print $1;}' | grep -v '(0000000000.000000)')
	if [ -z "$non_zero_time_stamp" ]; then
		echo ERROR[$setting_name]: good -\> test TEST log file \(TX\) has ONLY zero time stamps! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test TEST log file \(TX\) has found non-zero time stamp, OK! | tee -a "$meta_log_path"
	fi

	set -e


	#######################
	# both test <-> good
	#######################
	local both_sender_can_gen_flags="-L $frame_len -D i -b -g 0 -p 1 -n $max_frames"
	echo
	echo INFO[$setting_name]: Sending from both devices | tee -a "$meta_log_path"

	local both_file_test_name=both_file_test.log
	local both_file_test_path=$setting_log_dir/$both_file_test_name
	candump -n $(($max_frames*2)) $candump_options -H $can_test >$both_file_test_path &
	local both_test_candump_pid=$!

	local both_file_good_name=both_file_good.log
	local both_file_good_path=$setting_log_dir/$both_file_good_name
	# PEAK does not support hw timestamps for own tx messages
	candump -n $(($max_frames*2)) $candump_options $can_good >$both_file_good_path &
	local both_good_candump_pid=$!

	# wait a bit, else we may not get first frame
	sleep $startup_wait_s

	cangen $both_sender_can_gen_flags -I 2 $can_good &
	local both_good_cangen_pid=$!

	cangen $both_sender_can_gen_flags -I 1 $can_test &
	local both_test_cangen_pid=$!


	echo INFO[$setting_name]: Waiting for sends to finish | tee -a "$meta_log_path"

	#wait $both_good_cangen_pid $both_test_cangen_pid
	wait $both_good_cangen_pid
	wait $both_test_cangen_pid

	# brittle!
	sleep $candump_wait_s
	kill $both_test_candump_pid $both_good_candump_pid 2>/dev/null || true

	if [ $sort -ne 0 ]; then
		env LC_ALL=C sort $sort_options "$both_file_test_path" >"$both_file_test_path.sorted"
		env LC_ALL=C sort $sort_options "$both_file_good_path" >"$both_file_good_path.sorted"
		both_file_test_path="$both_file_test_path.sorted"
		both_file_good_path="$both_file_good_path.sorted"
	fi

	cat "$both_file_test_path" | awk '{ print $3; }' | grep "2##" | sed -E 's/0*(1|2)##//g' >"$setting_log_dir/both_test_send_by_good_content.log"
	cat "$both_file_test_path" | awk '{ print $3; }' | grep "1##" | sed -E 's/0*(1|2)##//g' >"$setting_log_dir/both_test_send_by_test_content.log"

	cat "$both_file_good_path" | awk '{ print $3; }' | grep "2##" | sed -E 's/0*(1|2)##//g' >"$setting_log_dir/both_good_send_by_good_content.log"
	cat "$both_file_good_path" | awk '{ print $3; }' | grep "1##" | sed -E 's/0*(1|2)##//g' >"$setting_log_dir/both_good_send_by_test_content.log"


	cat "$both_file_test_path" | grep "2##" | >"$setting_log_dir/both_test_send_by_good_timestamps.log"
	cat "$both_file_test_path" | grep "1##" | >"$setting_log_dir/both_test_send_by_test_timestamps.log"



	set +e

	lines=$(cat "$setting_log_dir/both_test_send_by_good_content.log" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: good -\> test TEST log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test TEST log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$setting_log_dir/both_test_send_by_test_content.log" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: test -\> test TEST log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> test TEST log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$setting_log_dir/both_good_send_by_good_content.log" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: good -\> test GOOD log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test GOOD log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	lines=$(cat "$setting_log_dir/both_good_send_by_test_content.log" | wc -l)
	if [ $max_frames -ne $lines ]; then
		echo ERROR[$setting_name]: test -\> good GOOD log file missing messages $lines/$max_frames! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> good GOOD log file $lines/$max_frames messages OK! | tee -a "$meta_log_path"
	fi

	same_messages "$setting_log_dir/both_good_send_by_good_content.log" "$setting_log_dir/both_test_send_by_good_content.log" 1
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: good -\> test messages DIFFER! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test messages OK! | tee -a "$meta_log_path"
	fi

	same_messages "$setting_log_dir/both_test_send_by_test_content.log" "$setting_log_dir/both_good_send_by_test_content.log" 1
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: test -\> good messages DIFFER! | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> good messages OK! | tee -a "$meta_log_path"
	fi

	mono_out=$("$script_dir/mono-check.py" "$setting_log_dir/both_test_send_by_good_timestamps.log" 2>&1)
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: good -\> test rx timestamps NOT mono! | tee -a "$meta_log_path"
		echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: good -\> test rx timestamps mono OK! | tee -a "$meta_log_path"
	fi

	mono_out=$("$script_dir/mono-check.py" "$setting_log_dir/both_test_send_by_test_timestamps.log" 2>&1)
	if [ $? -ne 0 ]; then
		echo ERROR[$setting_name]: test -\> good tx timestamps NOT mono! | tee -a "$meta_log_path"
		echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
		errors=$((errors+1))
	else
		echo INFO[$setting_name]: test -\> good tx timestamps mono OK! | tee -a "$meta_log_path"
	fi
	set -e

	if [ 0 -ne $test_jitter ]; then
		##############################################
		# good > test timestamp quality 1 [s]
		##############################################
		local ts_slow_max_frames=$(($seconds))
		local ts_slow_can_gen_flags="-e -I 123456 -L 64 -D r -g 1000 -b -n $ts_slow_max_frames"
		local ts_slow_jitter_threshold_ms=5

		echo
		echo INFO[$setting_name]: Sending from good -\> test with a fixed interval of 1 \[s\]
		local ts_slow_good_to_test_path=$setting_log_dir/ts_slow_good_to_test.log
		candump -n $(($ts_slow_max_frames)) -H -t a -L $can_test >"$ts_slow_good_to_test_path" &
		local ts_slow_good_to_test_test_pid=$!

		cangen $ts_slow_can_gen_flags $can_good

		sleep $candump_wait_s
		kill $ts_slow_good_to_test_test_pid 2>/dev/null || true

		set +e

		mono_out=$("$script_dir/mono-check.py" "$ts_slow_good_to_test_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: good -\> test RX timestamps NOT mono! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: good -\> test RX timestamps mono OK! | tee -a "$meta_log_path"
		fi


		mono_out=$("$script_dir/delta-check.py" --interval-ms 1000 --threshold-ms $ts_slow_jitter_threshold_ms "$ts_slow_good_to_test_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: good -\> test RX timestamps not within $ts_slow_jitter_threshold_ms \[ms\]! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: good -\> test RX timestamps jitter within $ts_slow_jitter_threshold_ms \[ms\] OK! | tee -a "$meta_log_path"
		fi

		set -e



		##############################################
		# test > good timestamp quality
		##############################################
		echo
		echo INFO[$setting_name]: Sending from test -\> good with a fixed interval of 1 \[s\]
		local ts_slow_test_to_good_path=$setting_log_dir/ts_slow_test_to_good.log
		candump -n $(($ts_slow_max_frames)) -H -t a -L $can_test >"$ts_slow_test_to_good_path" &
		local ts_slow_test_to_good_test_pid=$!

		cangen $ts_slow_can_gen_flags $can_test

		sleep $candump_wait_s
		kill $ts_slow_test_to_good_test_pid 2>/dev/null || true

		set +e

		mono_out=$("$script_dir/mono-check.py" "$ts_slow_test_to_good_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: test -\> good TX timestamps NOT mono! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: test -\> good TX timestamps mono OK! | tee -a "$meta_log_path"
		fi


		mono_out=$("$script_dir/delta-check.py" --interval-ms 1000 --threshold-ms $ts_slow_jitter_threshold_ms "$ts_slow_test_to_good_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: test -\> good TX timestamps not within $ts_slow_jitter_threshold_ms \[ms\]! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: test -\> good TX timestamps jitter within $ts_slow_jitter_threshold_ms \[ms\] OK! | tee -a "$meta_log_path"
		fi

		set -e


		##############################################
		# good > test timestamp quality 1 [ms]
		##############################################
		local ts_fast_max_frames=$(($seconds*1000))
		local ts_fast_can_gen_flags="-I 1 -L 0 -g 1 -n $ts_fast_max_frames"
		local ts_fast_jitter_threshold_ms=5

		echo
		echo INFO[$setting_name]: Sending from good -\> test with a fixed interval of 1 \[ms\]
		local ts_fast_good_to_test_path=$setting_log_dir/ts_fast_good_to_test.log
		candump -n $(($ts_fast_max_frames)) -H -t a -L $can_test >"$ts_fast_good_to_test_path" &
		local ts_fast_good_to_test_test_pid=$!

		cangen $ts_fast_can_gen_flags $can_good

		sleep $candump_wait_s
		kill $ts_fast_good_to_test_test_pid 2>/dev/null || true

		set +e

		mono_out=$("$script_dir/mono-check.py" "$ts_fast_good_to_test_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: good -\> test RX timestamps NOT mono! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: good -\> test RX timestamps mono OK! | tee -a "$meta_log_path"
		fi


		mono_out=$("$script_dir/delta-check.py" --interval-ms 1 --threshold-ms $ts_fast_jitter_threshold_ms "$ts_fast_good_to_test_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: good -\> test RX timestamps not within $ts_fast_jitter_threshold_ms \[ms\]! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: good -\> test RX timestamps jitter within $ts_fast_jitter_threshold_ms \[ms\] OK! | tee -a "$meta_log_path"
		fi

		set -e


		##############################################
		# test > good timestamp quality 1 [ms]
		##############################################


		echo
		echo INFO[$setting_name]: Sending from test -\> good with a fixed interval of 1 \[ms\]
		local ts_fast_test_to_good_path=$setting_log_dir/ts_fast_test_to_good.log
		candump -n $(($ts_fast_max_frames)) -H -t a -L $can_test >"$ts_fast_test_to_good_path" &
		local ts_fast_test_go_good_test_pid=$!

		cangen $ts_fast_can_gen_flags $can_test

		sleep $candump_wait_s
		kill $ts_fast_test_go_good_test_pid 2>/dev/null || true

		set +e

		mono_out=$("$script_dir/mono-check.py" "$ts_fast_test_to_good_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: test -\> good TX timestamps NOT mono! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: test -\> good TX timestamps mono OK! | tee -a "$meta_log_path"
		fi


		mono_out=$("$script_dir/delta-check.py" --interval-ms 1 --threshold-ms $ts_fast_jitter_threshold_ms "$ts_fast_test_to_good_path" 2>&1)
		if [ $? -ne 0 ]; then
			echo ERROR[$setting_name]: test -\> good TX timestamps not within $ts_fast_jitter_threshold_ms \[ms\]! | tee -a "$meta_log_path"
			echo ERROR[$setting_name]: $mono_out | tee -a "$meta_log_path"
			errors=$((errors+1))
		else
			echo INFO[$setting_name]: test -\> good TX timestamps jitter within $ts_fast_jitter_threshold_ms \[ms\] OK! | tee -a "$meta_log_path"
		fi

		set -e
	fi

}

if [ $seconds -gt 0 ]; then
	# Run tests for each setting
	for ((i=0; i<${#settings[@]}; i++)); do
		one_each ${settings[$i]} ${nominal_bitrates[$i]} ${data_bitrates[$i]}
	done
fi


#######################
# archive results
#######################
# tar_file=testresult-${date_str}.tar.xz

# echo
# echo INFO: creating archive $tar_file
# tar -C "$tmp_dir" -c . | pixz -9 >"$tmp_dir/$tar_file"

# if [ -n "$SUDO_UID" ]; then
# 	chown -R $SUDO_UID:$SUDO_GID $tmp_dir
# fi

# mv "$tmp_dir/$tar_file" $PWD

echo INFO: Finished with $errors errors. | tee -a "$meta_log_path"

pack_results

cleanup

echo INFO: Packing results and cleaning up.

exit $errors





