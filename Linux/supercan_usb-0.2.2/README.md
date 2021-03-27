# Building

Install `dkms` if you haven't already. This will take care of installing the headers for your kernels.

Then, from `Linux/supercan_usb-x.y.z`, do

```
make V=1 KERNELRELEASE=$(uname -r) -C /lib/modules/$(uname -r)/build M=$PWD

sudo modprobe can-dev
sudo rmmod supercan_usb 2>/dev/null || true && sudo insmod $PWD/supercan_usb.ko
```

