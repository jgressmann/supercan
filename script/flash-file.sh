#!/bin/bash


usage()
{
	echo $(basename $0) \[OPTIONS\] FILE...
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



jlink_options="-device ATSAME51J19 -if swd -JTAGConf -1,-1 -speed auto"


for i in ${POSITIONAL}; do
	echo Flashing $i...
	echo -e "eoe 1\nr\nloadfile $i\nr\ngo\nexit\n"| JLinkExe $jlink_options
done


