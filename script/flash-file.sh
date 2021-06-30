#!/bin/bash


usage()
{
	echo $(basename $0) \[OPTIONS\] FILE...
	echo "   -e, --erase    erase flash prior to uploading files"
	echo
	echo
}

erase=0

#https://stackoverflow.com/questions/192249/how-do-i-parse-command-line-arguments-in-bash
POSITIONAL=()
while [ $# -gt 0 ]; do
	key="$1"

 	case $key in
 		-h|--help)
			usage
			exit 0
			;;
		-e|--erase)
			erase=1
			shift # past argument
			;;
		*)    # unknown option
			POSITIONAL+=("$1") # save it in an array for later
			shift # past argument
 			;;
 	esac
done
set -- "${POSITIONAL[@]}" # restore positional parameters



jlink_options="-device ATSAME51J19 -if swd -JTAGConf -1,-1 -speed auto"


if [ $erase -ne 0 ]; then
	echo Erasing chip
	echo -e "r\nerase\nexit\n"| JLinkExe $jlink_options
fi

for i in ${POSITIONAL}; do
	echo Flashing $i...
	echo -e "eoe 1\nr\nloadfile $i\nr\ngo\nexit\n"| JLinkExe $jlink_options
done


