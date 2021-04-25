#!/bin/sh

usage()
{
	echo usage: $(basename $0) \[dir\]
	echo
	echo If omitted, the source directory is detected automatically.
	echo
}

script_dir=$(dirname $0)
base_dir=$script_dir/..
find_cmd="find $script_dir -type d -name 'supercan_usb-*'"

if [ $# -gt 0 ]; then
	if [ "-h" = "$1" ] || [ "--help" = "$1" ]; then
		usage
		exit 0
	fi

	if [ -d $1 ]; then
		supercan_dir=$1
		shift
	else
		echo ERROR: $1 is not a directory!
		exit 1
	fi
else
	dirs=$(eval $find_cmd | wc -l)
	#echo dirs $dirs
	if [ $dirs -eq 0 ]; then
		echo ERROR: no subdirectory found in $PWD!
		exit 1
	fi

	if [ $dirs -eq 1 ]; then
		supercan_dir=$(eval $find_cmd | head)
	else
		echo ERROR: found multiple directories in $PWD. Cannot use autodetection.
		exit 1
	fi
fi



cp "$base_dir/src/supercan.h" "$supercan_dir/"
