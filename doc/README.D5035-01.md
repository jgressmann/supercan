
# D5035-01 Firmware Update

This assumes you have a board with the SuperDFU bootloader already installed. If not, see below on how to build & flash the bootloader.

## Prequisites

Ensure you have `dfu-util` available on your system. Windows users can [download dfu-util binaries here](http://dfu-util.sourceforge.net/releases/). On a Debian derived distro such as Ubuntu, `apt install dfu-util` will get you set up.

## Flashing

### Linux

#### SuperCAN
```
sudo dfu-util -d 1d50:5035,:5036 -R -D supercan.dfu
```

#### SuperDFU

Since version 0.3.3, the bootloader can be updated through DFU. Earlier versions require a debug probe (see below).

```
sudo dfu-util -d 1d50:5035,:5036 -R -D superdfu.dfu
```

_NOTE: You likely need to re-flash the CAN application once the bootloader has been updated._

### Windows

Please follow [these steps](../Windows/README.firmware.md).
# Building

This section describes the steps to build the software in a Linux-like environment. Windows users should read [this](Windows/README.building.md).

## Setup

Clone this repository and initialize the submodules.

```
$ git submodule update --init --recursive
```


## Firmware

SuperCAN uses a customized [TinyUSB](https://github.com/hathach/tinyusb) stack.

You will need the the ARM GNU toolchain.
On Debian derived Linux distributions `apt-get install gcc-arm-none-eabi` will get you set up.

### Options

You can choose between these options

1. Build and flash stand-alone SuperCAN
2. Build and flash SuperCAN and SuperDFU (bootloader)
3. Build and upload SuperCAN through SuperDFU

If you have a debugger probe such as SEGGER's J-Link you can choose any option. For option 3 you need a board with the SuperDFU bootloader already flashed onto it.

### 1. Build and flash stand-alone SuperCAN

#### J-Link
```
$ cd Boards/examples/device/supercan
$ make -j V=1 BOARD=d5035_01 HWREV=3 flash-jlink
```

#### Atmel ICE
```
$ cd Boards/examples/device/supercan
$ make -j V=1 BOARD=d5035_01 HWREV=3 flash-edbg
```



This creates and flashes the firmware file. Make sure to replace _HWREV=3_ with the version of the board you are using.

### 2. Build and flash SuperCAN and SuperDFU (bootloader)

#### Prequisites

Ensure you have `python3` installed.

#### Build & flash

This option installs the SuperDFU  bootloader on the device. SuperDFU implements [USB DFU 1.1](https://usb.org/sites/default/files/DFU_1.1.pdf).

##### J-LINK

```
$ cd Boards/examples/device/atsame51_dfu
$ make -j V=1 BOARD=d5035_01 HWREV=3 BOOTLOADER=1 VID=0x1d50 PID=0x5036 PRODUCT_NAME="D5035-01 SuperCAN DFU" INTERFACE_NAME="D5035-01 SuperCAN DFU" flash-jlink
```

##### Atmel ICE

```
$ cd Boards/examples/device/atsame51_dfu
$ make -j V=1 BOARD=d5035_01 HWREV=3 BOOTLOADER=1 VID=0x1d50 PID=0x5036 PRODUCT_NAME="D5035-01 SuperCAN DFU" INTERFACE_NAME="D5035-01 SuperCAN DFU" flash-edbg
```

This creates and flashes the bootloader. Make sure to replace _HWREV=3_ with the revision of the board you are using.

Next, flash SuperCAN using these steps

##### J-LINK

```
$ cd Boards/examples/device/supercan
$ make -j V=1 BOARD=d5035_01 HWREV=3 APP=1 flash-dfu
```


##### Atmel ICE

```
$ cd Boards/examples/device/supercan
$ make -j V=1 BOARD=d5035_01 HWREV=3 APP=1 OFFSET=0x4000 edbg-dfu
```
### 3. Build and upload SuperCAN through SuperDFU

#### Prequisites

Ensure you have `python3` and `dfu-util` installed.

#### Build

Build the SuperCAN DFU file

```
$ cd Boards/examples/device/supercan
$ make -j V=1 BOARD=d5035_01 HWREV=3 APP=1 dfu
```

Ensure _HWREV_ matches the board you are using.

Next, upload the DFU file to the board.
```
$ cd Boards/examples/device/supercan
$ sudo dfu-util -R -D _build/build-d5035-01/d5035-01-firmware.dfu
```