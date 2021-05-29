#!/bin/sh

set -e
set -x

export DEBIAN_FRONTEND=noninteractive
export BOARD=d5035-01
export VID=0x1d50
export PID_RT=0x5035
export PID_DFU=0x5036
export MAKE_ARGS="-j V=1"
export BOOTLOADER_NAME="D5035-01 SuperCAN DFU"
export TARGET_DIR=$APPVEYOR_BUILD_FOLDER/tmp

hw_revs="$(seq -s ' ' 3)"

# install build dependencies
sudo apt-get update && sudo apt-get install -y dfu-util gcc-arm-none-eabi

# init submodules
git submodule update --init --recursive

env

# Save current commit
mkdir -p $TARGET_DIR/supercan
echo $APPVEYOR_REPO_COMMIT >$TARGET_DIR/supercan/COMMIT

# make output dirs for hw revs
for i in $hw_revs; do
	mkdir -p $TARGET_DIR/supercan/$BOARD/0$i
done


# SuperDFU
cd $APPVEYOR_BUILD_FOLDER/Boards/examples/device/atsame51_dfu

for i in $hw_revs; do
	make $MAKE_ARGS BOARD=$BOARD BOOTLOADER=1 VID=$VID PID=$PID_DFU PRODUCT_NAME="$BOOTLOADER_NAME" INTERFACE_NAME="$BOOTLOADER_NAME" HWREV=$i
	cp _build/build-$BOARD/$BOARD-firmware.hex $TARGET_DIR/supercan/$BOARD/0$i/superdfu.hex
	rm -rf _build
done

# SuperCAN
cd $APPVEYOR_BUILD_FOLDER/Boards/examples/device/supercan

for i in $hw_revs; do
	make $MAKE_ARGS HWREV=$i
	cp _build/build-$BOARD/$BOARD-firmware.hex $TARGET_DIR/supercan/$BOARD/0$i/supercan-standalone.hex
	rm -rf _build
	make $MAKE_ARGS HWREV=$i APP=1 dfu
	cp _build/build-$BOARD/$BOARD-firmware.superdfu.hex $TARGET_DIR/supercan/$BOARD/0$i/supercan-dfu.hex
	cp _build/build-$BOARD/$BOARD-firmware.dfu $TARGET_DIR/supercan/$BOARD/0$i/supercan.dfu
	rm -rf _build
done

# archive
cd $TARGET_DIR && tar cfz supercan-firmware.tar.gz supercan

# find $TARGET_DIR || true