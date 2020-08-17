# Building

## DKMS

## Standalone

make V=1 KERNELRELEASE=$(uname -r) -C /lib/modules/$(uname -r)/build M=$PWD

sudo modprobe can-dev
sudo rmmod supercan_usb 2>/dev/null || true && sudo insmod $PWD/supercan_usb.ko

