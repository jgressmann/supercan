# DKMS setup

Install `dkms` if you haven't already.

## Add, build & install the kernel module

```
$ <path to supercan>/Linux/dkms-init.sh
$ sudo dkms add <path to supercan>/Linux/supercan_usb-<version>
$ sudo dkms build supercan_usb/<version>
$ sudo dkms install supercan_usb/<version>
```

## Uninstall and remove the kernel module

$ sudo dkms uninstall supercan_usb/<version>
$ sudo dkms remove supercan_usb/<version> --all
```

