# SuperCAN

What is this?

This is project SuperCAN. An open source USB to CAN-FD protocol. SuperCAN works with the open source [D5035-01](https://github.com/RudolphRiedel/USB_CAN-FD) hardware to form a working USB 2.0 to CAN-FD interface.

# Status

SuperCAN works on Windows 10. Linux drivers are being developed.

To use a SuperCAN device on Windows, simply plug it in.

#

# Building

## Setup

Clone this repository and initialize the submodules.

```
$ git submodule update --init --recursive
```



## 1. Firmware

SuperCAN uses (a customized) [TinyUSB](https://github.com/hathach/tinyusb) stack.

I am assuming you are on Linux and are using a Debian derived distribution.

### Options

You can choose between these options

1. Build and flash stand-alone SuperCAN
2. Build and flash SuperCAN and SuperDFU (bootloader)
3. Build and upload SuperCAN through SuperDFU

If you have a debugger probe such as SEGGER's J-Link you can choose any option. For option 3 you need a board with the SuperDFU bootloader already flashed onto it.

### 1. Build and flash stand-alone SuperCAN

```
$ cd Boards/examples/device/supercan
$ VERBOSE=1 make V=1 BOARD=d5035-01 HWREV=2 flash-jlink
```

This create and flashes the firmware file. Make sure to replace _HWREV=2_ with the version of the board you are using.

### 2. Build and flash SuperCAN and SuperDFU (bootloader)

#### Prequisites

Ensure you have `python3` installed.

#### Build & flash

This option installs the SuperDFU  bootloader on the device. SuperDFU implements [USB DFU 1.1](https://usb.org/sites/default/files/DFU_1.1.pdf).

```
$ cd Boards/examples/device/atsame51_dfu
$ VERBOSE=1 make V=1 BOARD=d5035-01 HWREV=2 BOOTLOADER=1 flash-jlink
```

This creates and flashes the bootloader. Make sure to replace _HWREV=2_ with the version of the board you are using.

Next, flash SuperCAN using these steps

```
$ cd Boards/examples/device/supercan
$ VERBOSE=1 make V=1 BOARD=d5035-01 HWREV=2 APP=1 flash-dfu
```

### 3. Build and upload SuperCAN through SuperDFU

#### Prequisites

Ensure you have `python3` and `fwupd` installed. The latter provides `dfu-tool`.

#### Build

Build the SuperCAN DFU file

```
$ cd Boards/examples/device/supercan
$ VERBOSE=1 make V=1 BOARD=d5035-01 HWREV=2 APP=1 dfu
```

Ensure _HWREV_ matches the board you are using.

Next, upload the DFU file to the board.
```
$ cd Boards/examples/device/supercan
$ sudo dfu-tool -v write _build/build-d5035-01/d5035-01-firmware.dfu
```

## 2. Windows API

Simply build the Visual Studio solution in the Windows folder. I use Visual Studio Community 2019 available for free for non-commercial products (as of this writing).

The solution contains code for a demo application that sends and dumps CAN traffic.


# License

SuperCAN is licenses under the MIT license. Supercan uses FreeRTOS and TinyUSB which are both available under the same license.