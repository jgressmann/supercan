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
sudo apt-get update && sudo apt-get install -y dfu-util gcc-arm-none-eabi pixz python3

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

	# generate J-Link flash script
	cat <<EOF >$TARGET_DIR/supercan/$BOARD/0$i/superdfu.jlink
r
loadfile superdfu.hex
r
go
exit
EOF

	# generate README
	cat <<EOF >>$TARGET_DIR/supercan/$BOARD/0$i/README.md
# SuperCAN Device Firmware

## Content
### Device Bootloader (SuperDFU)

- superdfu.bin: binary, flash with debug probe
- superdfu.hex: hex, flash with debug probe
- superdfu.dfu: update with dfu-util
- superdfu.jlink: J-Link flash script

EOF

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

	# generate J-Link flash script (standalone)
	cat <<EOF >$TARGET_DIR/supercan/$BOARD/0$i/superdfu-standalone.jlink
r
loadfile supercan-standalone.hex
r
go
exit
EOF

	# generate J-Link flash script (requires bootloader)
	cat >$TARGET_DIR/supercan/$BOARD/0$i/superdfu-dfu.jlink <<EOF
r
loadfile supercan-dfu.hex
r
go
exit
EOF


	cat <<EOF >>$TARGET_DIR/supercan/$BOARD/0$i/README
### CAN Application (SuperCAN)

- supercan-standalone.bin: binary, no bootloader required, flash with debug probe
- supercan-standalone.hex: hex, no bootloader required, flash with debug probe
- supercan-standalone.jlink: J-Link flash script
- supercan-dfu.bin: binary, requires bootloader, flash with debug probe to 0x4000
- supercan-dfu.hex: hex, requires bootloader, flash with debug probe to 0x4000
- supercan-dfu.jlink: J-Link flash script
- supercan.dfu: requires bootloader, update with dfu-util


## Installation / Upgrade

If you are using the bootloader, update it *first*.
After the bootloader update, you likely need to re-install the CAN application.

### Device Bootloader

#### J-LINK

\`\`\`
JLinkExe -device ATSAME51J18 -if swd -JTAGConf -1,-1 -speed auto -CommandFile superdfu.jlink
\`\`\`


### CAN Application (no Bootloder Required)

#### J-LINK

\`\`\`
JLinkExe -device ATSAME51J18 -if swd -JTAGConf -1,-1 -speed auto -CommandFile supercan-standalone.jlink
\`\`\`

### CAN Application (Bootlodaer Required)

#### J-LINK

\`\`\`
JLinkExe -device ATSAME51J18 -if swd -JTAGConf -1,-1 -speed auto -CommandFile supercan-dfu.jlink
\`\`\`

EOF

done
unset hw_revs

########################
# SAM E54 Xplained Pro #
########################

export BOARD=same54xplainedpro

mkdir -p $TARGET_DIR/supercan/$BOARD

make $MAKE_ARGS

cp _build/$BOARD/${project}.hex $TARGET_DIR/supercan/$BOARD/
cp _build/$BOARD/${project}.bin $TARGET_DIR/supercan/$BOARD/
rm -rf _build

# generate J-Link flash script (standalone)
cat <<EOF >$TARGET_DIR/supercan/$BOARD/README.md
# SuperCAN Device Firmware

## Content
- supercan.bin: binary, flash with debug probe
- supercan.hex: hex, flash with debug probe

## Installation
### EDBG

\`\`\`
edbg --verbose -t same54 -pv -f supercan.bin
\`\`\`

EOF


##########################
# Other Boards (bin/hex) #
##########################


boards="teensy_40 d5035_03"
for board in $boards; do
	export BOARD=$board

	mkdir -p $TARGET_DIR/supercan/$BOARD

	make $MAKE_ARGS

	cp _build/$BOARD/${project}.hex $TARGET_DIR/supercan/$BOARD/
	cp _build/$BOARD/${project}.bin $TARGET_DIR/supercan/$BOARD/
	rm -rf _build
done

######################
# Other Boards (uf2) #
######################

boards="feather_m4_can_express"
for board in $boards; do
	export BOARD=$board

	mkdir -p $TARGET_DIR/supercan/$BOARD

	make $MAKE_ARGS uf2

	cp _build/$BOARD/${project}.uf2 $TARGET_DIR/supercan/$BOARD/
	rm -rf _build
done

# archive
cd $TARGET_DIR && (tar c supercan | pixz -9 >supercan-firmware.tar.xz)

# find $TARGET_DIR || true
