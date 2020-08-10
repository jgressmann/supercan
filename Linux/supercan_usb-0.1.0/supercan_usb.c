/*
 * Copyright (c) 2020 Jean Gressmann <jean@0x42.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */


#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>

#define SC_PACKED __packed
#include "supercan.h"

#if defined(__LITTLE_ENDIAN)
#define SC_NATIVE_BYTE_ORDER SC_BYTE_ORDER_LE
#elif defined(__BIG_ENDIAN)
#define SC_NATIVE_BYTE_ORDER SC_BYTE_ORDER_BE
#else
#error Unsupported byte order
#endif

#define SC_USB_TIMEOUT_MS 1000
#define SC_MIN_SUPPORTED_TRANSFER_SIZE 64

struct sc_usb_dev;
struct sc_can_chan {
	struct sc_usb_dev *dev;
	struct usb_anchor rx_queued;
	u8 cmd_epp;
	u8 msg_epp;
	u8 initialized;
};

/* usb interface struct */
struct sc_usb_dev {
	struct sc_can_chan *chan_ptr;
	struct usb_device *udev;
	u16 (*host_to_dev16)(u16);
	u32 (*host_to_dev32)(u32);
	// u8 cmd_epp;
	u16 msg_buffer_size;
	u8 chan_count;
};

static u16 sc_nop16(u16 value)
{
	return value;
}

static u32 sc_nop32(u32 value)
{
	return value;
}

static u16 sc_swap16(u16 value)
{
	return __builtin_bswap16(value);
}

static u32 sc_swap32(u32 value)
{
	return __builtin_bswap32(value);
}

static void sc_can_chan_uninit(struct sc_can_chan *ch)
{
	if (WARN_ON(!ch->dev))
		return;


}

static int sc_can_chan_init(struct sc_can_chan *ch, struct sc_chan_info *info)
{
	if (WARN_ON(!ch->dev))
		return -ENODEV;

	ch->cmd_epp = info->cmd_epp;
	ch->msg_epp = info->msg_epp;
	return -ENODEV;
}


static void sc_usb_cleanup(struct sc_usb_dev *dev)
{
	unsigned i;
	if (dev) {
		if (dev->chan_ptr) {
			for (i = 0; i < dev->chan_count; ++i)
				sc_can_chan_uninit(&dev->chan_ptr[i]);

			kfree(dev->chan_ptr);
		}

		kfree(dev);
	}
}

static void sc_usb_disconnect(struct usb_interface *intf)
{
	struct sc_usb_dev *dev = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (dev)
		sc_usb_cleanup(dev);
}

static int sc_usb_probe(
	struct usb_interface *intf,
	const struct usb_device_id *id)
{
	int rc = 0;
	int actual_len;
	struct sc_msg_req *req;
	struct sc_msg_hello *device_hello;
	struct sc_msg_dev_info *info;
	uint8_t cmd_epp;
	void *tx_buf = NULL;
	void *rx_buf = NULL;
	u16 ep_size;
	u16 msg_buffer_size;
	unsigned i;
	char serial_str[1 + sizeof(info->sn_bytes)*2];
	struct sc_usb_dev *dev = NULL;
	struct usb_device *udev = interface_to_usbdev(intf);

	// ensure interface is set
	if (unlikely(!intf->cur_altsetting)) {
		rc = -ENODEV;
		goto err;
	}

	// we want a VENDOR interface
	if (intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC) {
		dev_dbg(&intf->dev, "not a vendor specific interface: %#02x\n", intf->cur_altsetting->desc.bInterfaceClass);
		rc = -ENODEV;
		goto err;
	}

	// must have at least one endpoint
	dev_dbg(&intf->dev, "device has %u eps\n", intf->cur_altsetting->desc.bNumEndpoints);
	if (unlikely(!(intf->cur_altsetting->desc.bNumEndpoints / 2))) {
		// no endpoint pair
		rc = -ENODEV;
		goto err;
	}

	// endpoint must be bulk
	if (unlikely(usb_endpoint_type(&intf->cur_altsetting->endpoint->desc) != USB_ENDPOINT_XFER_BULK)) {
		rc = -ENODEV;
		goto err;
	}

	ep_size = le16_to_cpu(intf->cur_altsetting->endpoint->desc.wMaxPacketSize);
	dev_dbg(&intf->dev, "ep size %u\n", ep_size);

	// endpoint too small?
	if (unlikely(ep_size < SC_MIN_SUPPORTED_TRANSFER_SIZE)) {
		rc = -ENODEV;
		goto err;
	}

	cmd_epp = usb_endpoint_num(&intf->cur_altsetting->endpoint->desc);
	dev_dbg(&intf->dev, "ep num %u\n", cmd_epp);
	tx_buf = kmalloc(ep_size, GFP_KERNEL);
	if (!tx_buf) {
		rc = -ENOMEM;
		goto err;
	}

	req = tx_buf;
	memset(req, 0, sizeof(*req));
	req->id = SC_MSG_HELLO_DEVICE;
	req->len = sizeof(*req);


	dev_dbg(&intf->dev, "sending SC_MSG_HELLO_DEVICE\n");
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, cmd_epp), req,
			sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	rx_buf = kmalloc(ep_size, GFP_KERNEL);
	if (!rx_buf) {
		rc = -ENOMEM;
		goto err;
	}

	/* wait for response */
	dev_dbg(&intf->dev, "waiting for SC_MSG_HELLO_HOST\n");
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, cmd_epp), rx_buf, ep_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	device_hello = rx_buf;

	if (actual_len < 0 ||
	    (size_t)actual_len < sizeof(struct sc_msg_hello) ||
	    SC_MSG_HELLO_HOST != device_hello->id ||
	    device_hello->len < sizeof(struct sc_msg_hello)) {
		/* no dice */
		rc = -ENODEV;
		goto err;
	}

	msg_buffer_size = ntohs(device_hello->msg_buffer_size);

	if (0 == device_hello->proto_version || msg_buffer_size < SC_MIN_SUPPORTED_TRANSFER_SIZE) {
		dev_err(&intf->dev,
			"badly configured device: proto_version=%u msg_buffer_size=%u\n",
			device_hello->proto_version, msg_buffer_size);
		rc = -ENODEV;
		goto err;
	}

	if (device_hello->proto_version > SC_VERSION) {
		dev_info(&intf->dev, "device proto version %u exceeds supported version %u\n",
			device_hello->proto_version, SC_VERSION);
		rc = -ENODEV;
		goto err;
	}

	/* At this point we are fairly confident we are dealing with the genuine article. */
	dev_info(
		&intf->dev,
		"device proto version %u, %s endian\n",
		device_hello->proto_version, device_hello->byte_order == SC_BYTE_ORDER_LE ? "little" : "big");



	req->id = SC_MSG_DEVICE_INFO;
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, cmd_epp), req, sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	/* wait for response */
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, cmd_epp), rx_buf, ep_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	info = rx_buf;

	if (unlikely(actual_len < 0 ||
				 (size_t)actual_len < sizeof(*info) ||
	             SC_MSG_DEVICE_INFO != info->id ||
	             info->len < sizeof(*info) ||
	             actual_len < sizeof(*info)
	                + sizeof(struct sc_chan_info) * info->chan_count)) {
		dev_err(&intf->dev, "bad reply to SC_MSG_DEVICE_INFO (%d bytes)\n", actual_len);
		rc = -ENODEV;
		goto err;
	}

	// dev_dbg(&intf->dev, "info serial len %u, str buf size %zu\n", info->sn_len, ARRAY_SIZE(serial_str));
	for (i = 0; i < min((size_t)info->sn_len, ARRAY_SIZE(serial_str)-1); ++i)
		snprintf(&serial_str[i*2], 3, "%02x", info->sn_bytes[i]);

	// dev_dbg(&intf->dev, "i=%u\n", i);
	serial_str[i*2] = 0;

	dev_info(&intf->dev, "device serial %s has %u channel(s)\n", serial_str, info->chan_count);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		rc = -ENODEV;
		goto err;
	}

	dev->udev = interface_to_usbdev(intf);

	if (SC_NATIVE_BYTE_ORDER == device_hello->byte_order) {
		dev->host_to_dev16 = &sc_nop16;
		dev->host_to_dev32 = &sc_nop32;
	} else {
		dev->host_to_dev16 = &sc_swap16;
		dev->host_to_dev32 = &sc_swap32;
	}

	dev->msg_buffer_size = msg_buffer_size;
	dev->chan_count = info->chan_count;
	dev->chan_ptr = kcalloc(dev->chan_count, sizeof(*dev->chan_ptr), GFP_KERNEL);
	if (!dev->chan_ptr) {
		rc = -ENODEV;
		goto err;
	}

	for (i = 0; i < dev->chan_count; ++i) {
		dev->chan_ptr[i].dev = dev;
		rc = sc_can_chan_init(&dev->chan_ptr[i], &info->chan_info[i]);
		if (rc)
			goto err;
	}

	usb_set_intfdata(intf, dev);

cleanup:
	kfree(tx_buf);
	kfree(rx_buf);

	return rc;

err:
	sc_usb_cleanup(dev);
	goto cleanup;
}

MODULE_AUTHOR("Jean Gressmann <jean@0x42.de>");
MODULE_DESCRIPTION("Driver for the SuperCAN family of CAN(-FD) interfaces");
MODULE_LICENSE("GPL v2");



static const struct usb_device_id sc_usb_device_id_table[] = {
	{ USB_DEVICE(0x4243, 0x0002) },
	{ }
};

MODULE_DEVICE_TABLE(usb, sc_usb_device_id_table);

static struct usb_driver sc_usb_driver = {
	.name = "supercan_usb",
	.probe = sc_usb_probe,
	.disconnect = sc_usb_disconnect,
	.id_table = sc_usb_device_id_table,
};

module_usb_driver(sc_usb_driver);