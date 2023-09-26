# SuperCAN

[![License](https://img.shields.io/badge/license-MIT-brightgreen.svg)](https://opensource.org/licenses/MIT)


## What is this?

This is project SuperCAN. An open source USB to CAN-FD protocol.
SuperCAN works with [these devices](doc/README.devices.rst) to form an USB to CAN-FD interface.

## Builds

[![Build status](https://github.com/jgressmann/supercan/actions/workflows/build.yml/badge.svg)

## Supported Devices

Have a look [here](doc/README.devices.rst).

## Supported Operating Systems

SuperCAN supports Windows 10 and Linux.


## Apps

### Windows

- [CAN++](https://github.com/TDahlmann/canpp)
- [CANdevStudio](https://github.com/GENIVI/CANdevStudio)

_NOTE: Kindly ensure you have the device driver package installed on your system. Ensure you have copied the plugin from the Windows archive into the plugin folder of CANdevStudio._

### Linux

- [can-utils](https://github.com/linux-can/can-utils)
- [CANdevStudio](https://github.com/GENIVI/CANdevStudio)

_NOTE: Kindly ensure you have the device driver built and loaded into the kernel. See below._


## Windows API & demo app

Build the Visual Studio solution in the Windows folder. I use Visual Studio Community 2019 which is available free of charge for non-commercial products (as of this writing).

The solution contains code for a demo application that sends and dumps CAN traffic.

## Linux SocketCAN driver

To build the Linux kernel module, please follow [these instructions](https://github.com/jgressmann/supercan-linux).

# License

SuperCAN is available under the MIT license. SuperCAN uses FreeRTOS and TinyUSB which are both available under the same license.

The Linux driver is available under GPLv2 or MIT.
