#!/bin/sh

script_dir=$(dirname $0)
supercan_dir=$(find "$script_dir" -type d -name 'supercan_usb-*' | head)

rm -f "$supercan_dir/sc.h"
dst=$(readlink -f "$supercan_dir/supercan.h")
cp "$dst" "$supercan_dir/sc.h"
