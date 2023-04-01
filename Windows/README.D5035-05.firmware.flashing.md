# D5035-05 Firmware Update Guide for Windows

Ensure you have [dfu-util](http://dfu-util.sourceforge.net/releases/) available.

## SuperCAN Firmware Update Guide

1. Plug in the device.
2. Open a terminal window (`cmd.exe`).
3. Flash the CAN application.

	`dfu-util -e -R -a 0 --dfuse-address 0x08000000 -D supercan-app.bin`

4. Replug the device to return to normal operation.


If flashing fails in step 3 with `Lost device after RESET`, you likely need to set up WinUSB for STM32 with Zadig. See next section for details.


### Zadig Driver Setup

Ensure you have [Zadig](https://zadig.akeo.ie/) available on your system.

1. Plug in the device.
2. Open Zadig (as Administrator).
3. From the menu, select _Options_ -> _List All Devices_.
4. Select `DFU in FS Mode` in the combo box.

	Select `WinUSB (v6.1)` as the new driver, then press `Replace Driver`.

	![`DFU in FS Mode` selected in Zadig](doc/zadig-stm32-dfu-mode.png)

5. You should now have WinUSB set up for your device and can continue with the firmware update.


## Troubleshooting

Its not always rainbows and unicorns though, is it?

### Flashing with dfu-util 0.10 fails

This version seems to misinterpret the vendor/product ID found in the suffix of the firmware file.
Strangely enough, _dfu-suffix_ reports the proper values:

```
dfu-suffix.exe -c supercan.dfu
dfu-suffix (dfu-util) 0.10

Copyright 2011-2012 Stefan Schmidt, 2013-2020 Tormod Volden
This program is Free Software and has ABSOLUTELY NO WARRANTY
Please report bugs to http://sourceforge.net/p/dfu-util/tickets/

The file supercan.dfu contains a DFU suffix with the following properties:
BCD device:     0xFFFF
Product ID:     0x5035
Vendor ID:      0x1D50
BCD DFU:        0x0100
Length:         16
CRC:            0xF30CA4DD
```

#### Fix

Use version 0.9 of dfu-util or version 0.11.

### Windows refuses to run `dfu-util`.

Yeah, that happens for some reason.

#### Fix

Try `dfu-util-static` that should work.

## Hints

* Have the _Device Manager_ open while performing driver assignment.
	* Enable the display of _Hidden Devices_ through the _Views_ menu.
	* You can _undo_ a driver assignment performed through _Zadig_ by selecting the device, then pressing _Del_ on the keyboard, and then confirming that you want to _Uninstall driver software_. You may need to replug the device afterwards.

