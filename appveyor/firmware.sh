#!/bin/sh

set -e
set -x

if [ -z "$APPVEYOR_BUILD_FOLDER" ]; then
	echo "ERROR: APPVEYOR_BUILD_FOLDER not set"
	exit 1
fi

export DEBIAN_FRONTEND=noninteractive

export VID=0x1d50
export PID_RT=0x5035
export PID_DFU=0x5036
export MAKE_ARGS="-j V=1"
export BOOTLOADER_NAME="D5035-01 SuperCAN DFU"
export TARGET_DIR=$APPVEYOR_BUILD_FOLDER/tmp



# install build dependencies
sudo apt-get update && sudo apt-get install -y dfu-util gcc-arm-none-eabi pixz

# init submodules
git submodule update --init --depth 1 --recursive

env

# Save current commit
mkdir -p $TARGET_DIR/supercan
echo $APPVEYOR_REPO_COMMIT >$TARGET_DIR/supercan/COMMIT

############
# D5035-01 #
############
hw_revs=3
export BOARD=d5035_01

# make output dirs for hw revs
for i in $hw_revs; do
	mkdir -p $TARGET_DIR/supercan/$BOARD/0$i
done



# SuperDFU
project=atsame51_dfu
cd $APPVEYOR_BUILD_FOLDER/Boards/examples/device/${project}


for i in $hw_revs; do
	make $MAKE_ARGS BOARD=$BOARD BOOTLOADER=1 VID=$VID PID=$PID_DFU PRODUCT_NAME="$BOOTLOADER_NAME" INTERFACE_NAME="$BOOTLOADER_NAME" HWREV=$i
	cp _build/$BOARD/${project}.hex $TARGET_DIR/supercan/$BOARD/0$i/superdfu.hex
	cp _build/$BOARD/${project}.bin $TARGET_DIR/supercan/$BOARD/0$i/superdfu.bin
	rm -rf _build
	make $MAKE_ARGS BOARD=$BOARD BOOTLOADER=1 VID=$VID PID=$PID_DFU PRODUCT_NAME="$BOOTLOADER_NAME" INTERFACE_NAME="$BOOTLOADER_NAME" HWREV=$i APP=1 dfu
	cp _build/$BOARD/${project}.dfu $TARGET_DIR/supercan/$BOARD/0$i/superdfu.dfu
	rm -rf _build
done

# SuperCAN
project=supercan
cd $APPVEYOR_BUILD_FOLDER/Boards/examples/device/${project}


for i in $hw_revs; do
	make $MAKE_ARGS HWREV=$i
	cp _build/$BOARD/${project}.hex $TARGET_DIR/supercan/$BOARD/0$i/supercan-standalone.hex
	cp _build/$BOARD/${project}.bin $TARGET_DIR/supercan/$BOARD/0$i/supercan-standalone.bin
	rm -rf _build
	make $MAKE_ARGS HWREV=$i APP=1 dfu
	cp _build/$BOARD/${project}.superdfu.hex $TARGET_DIR/supercan/$BOARD/0$i/supercan-dfu.hex
	cp _build/$BOARD/${project}.superdfu.bin $TARGET_DIR/supercan/$BOARD/0$i/supercan-dfu.bin
	cp _build/$BOARD/${project}.dfu $TARGET_DIR/supercan/$BOARD/0$i/supercan.dfu
	rm -rf _build
done
unset hw_revs

################
# Other Boards #
################


boards="teensy_40 d5035_03 same54xplainedpro"
for board in $boards; do
	export BOARD=$board

	mkdir -p $TARGET_DIR/supercan/$BOARD

	make $MAKE_ARGS

	cp _build/$BOARD/${project}.hex $TARGET_DIR/supercan/$BOARD/supercan.hex
	cp _build/$BOARD/${project}.bin $TARGET_DIR/supercan/$BOARD/supercan.bin
	rm -rf _build
done

# archive
cd $TARGET_DIR && (tar c supercan | pixz -9 >supercan-firmware.tar.xz)

# find $TARGET_DIR || true
