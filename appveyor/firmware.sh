#!/bin/sh

set -e
set -x

if [ -z "$APPVEYOR_BUILD_FOLDER" ]; then
	echo "ERROR: APPVEYOR_BUILD_FOLDER not set"
	exit 1
fi

export DEBIAN_FRONTEND=noninteractive



# install build dependencies
sudo apt-get update && sudo apt-get install -y dfu-util gcc-arm-none-eabi pixz python3

# init submodules
git submodule update --init --depth 1 --recursive

env

$APPVEYOR_BUILD_FOLDER/Boards/examples/device/supercan/build.sh
mv $APPVEYOR_BUILD_FOLDER/Boards/examples/device/supercan/firmware/supercan-firmware.tar.xz $APPVEYOR_BUILD_FOLDER
