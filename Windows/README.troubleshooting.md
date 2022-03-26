# Troubleshooting Guide for Windows

>Have you tried turning it it off and on?

## Checklist

### Device
- Update the device to the latest firmware
- Re-plug your device
- Plug your device directly into the computer
- Verify your USB cable isn't bad
- Is the device properly recognized by Windows? Check the device manager, there should be some USB whose name contains `SuperCAN`.

### Computer
- Update the driver software to the latest release.
- Reboot the computer
- Set API log levels to maximum
- Watch for SuperCAN (SC) debug messages using [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview).
  This one is especially relevant if using the COM server.
- Does it work with the demo application?


