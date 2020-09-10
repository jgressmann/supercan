# SuperCAN

[![License](https://img.shields.io/badge/license-MIT-brightgreen.svg)](https://opensource.org/licenses/MIT)

What is this?

This is project SuperCAN. An open source USB to CAN-FD protocol.
SuperCAN works with the open source [D5035-01](https://github.com/RudolphRiedel/USB_CAN-FD)
hardware to form a working USB 2.0 to CAN-FD interface.

# Status

SuperCAN supports Windows 10 and Linux. The protocol should be considered in alpha stadium as the driver code for both platforms is maturing. With the release of version 1.0.0 the protocol will be frozen.

To use a SuperCAN device, simply plug it in.

## Build

Build        | Status
------------ | -------------
*Firmware*   | [![Build status](https://ci.appveyor.com/api/projects/status/i398eskxl418rwf9?svg=true)](https://ci.appveyor.com/project/jgressmann/supercan-firmware)
*Linux*      | [![Build status](https://ci.appveyor.com/api/projects/status/knw9udgvlal4u3b0?svg=true)](https://ci.appveyor.com/project/jgressmann/supercan-linux)
*Windows*    | [![Build status](https://ci.appveyor.com/api/projects/status/p25qholxtadg71ej?svg=true)](https://ci.appveyor.com/project/jgressmann/supercan-windows)



# Building

## Setup

Clone this repository and initialize the submodules.

```
$ git submodule update --init --recursive
```


## 1. Firmware

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

```
$ cd Boards/examples/device/supercan
$ make V=1 HWREV=3 flash-jlink
```

This creates and flashes the firmware file. Make sure to replace _HWREV=3_ with the version of the board you are using.

### 2. Build and flash SuperCAN and SuperDFU (bootloader)

#### Prequisites

Ensure you have `python3` installed.

#### Build & flash

This option installs the SuperDFU  bootloader on the device. SuperDFU implements [USB DFU 1.1](https://usb.org/sites/default/files/DFU_1.1.pdf).

```
$ cd Boards/examples/device/atsame51_dfu
$ make V=1 BOARD=d5035-01 HWREV=3 BOOTLOADER=1 VID=0x1d50 PID=0x5035 flash-jlink
```

This creates and flashes the bootloader. Make sure to replace _HWREV=3_ with the revision of the board you are using.

Next, flash SuperCAN using these steps

```
$ cd Boards/examples/device/supercan
$ make V=1 HWREV=3 APP=1 flash-dfu
```

### 3. Build and upload SuperCAN through SuperDFU

#### Prequisites

Ensure you have `python3` and `fwupd` installed. The latter provides dfu-tool.

#### Build

Build the SuperCAN DFU file

```
$ cd Boards/examples/device/supercan
$ make V=1 HWREV=3 APP=1 dfu
```

Ensure _HWREV_ matches the board you are using.

Next, upload the DFU file to the board.
```
$ cd Boards/examples/device/supercan
$ sudo  dfu-tool write _build/build-d5035-01/d5035-01-firmware.dfu
```

## 2. Windows API

Simply build the Visual Studio solution in the Windows folder. I use Visual Studio Community 2019 which is available free of charge for non-commercial products (as of this writing).

The solution contains code for a demo application that sends and dumps CAN traffic.

## 3. Linux

To build the Linux kernel module follow [these instructions](Linux/supercan_usb-0.1.0/README.md).

# License

SuperCAN is available under the MIT license. SuperCAN uses FreeRTOS and TinyUSB which are both available under the same license.

The Linux driver is available under GPLv2.
