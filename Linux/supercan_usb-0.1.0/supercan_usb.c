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
#define SC_MAX_RX_URBS 8
#define SC_MAX_TX_URBS 8

struct sc_usb_device_data;
struct sc_can_channel_data {
	struct sc_usb_device_data *usb;
	struct net_device *net;
	struct usb_anchor rx_anchor;
	struct usb_anchor tx_anchor;
	struct can_berr_counter bec;
	u8 cmd_epp;
	u8 msg_epp;
};

/* usb interface struct */
struct sc_usb_device_data {
	struct sc_can_channel_data *chan_ptr;
	struct usb_interface *intf;
	u16 (*host_to_dev16)(u16);
	u32 (*host_to_dev32)(u32);
	u32 can_clock_hz;
	u32 ctrlmode_static;
	u32 ctrlmode_supported;
	struct can_bittiming_const nominal;
	struct can_bittiming_const data;
	u16 features;
	u16 cmd_buffer_size;
	u16 msg_buffer_size;
	u8 chan_count;
};

struct sc_net_device_data {
	struct can_priv can; // must be first member I suppose
	struct sc_can_channel_data *ch;
	u8 registered;
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

static int sc_can_stop(struct net_device *dev)
{
	struct sc_net_device_data *net_data = netdev_priv(dev);
	struct sc_can_channel_data *ch = net_data->ch;

	net_data->can.state = CAN_STATE_STOPPED;
	(void)close_candev(dev);

	usb_kill_anchored_urbs(&ch->rx_anchor);

	return 0;
}

static void sc_usb_process_rx_buffer(struct sc_can_channel_data *ch, u8 *ptr, unsigned size)
{

}

static void sc_usb_urb_completed(struct urb *urb)
{
	struct sc_can_channel_data *ch;
	struct usb_device *udev;
	int rc;

	BUG_ON(!urb);
	BUG_ON(!urb->context);

	ch = (struct sc_can_channel_data *)urb->context;

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -EPIPE:
	case -EPROTO:
	case -ESHUTDOWN:
		return;
	default:
		dev_info(&ch->usb->intf->dev, "rx URB status %d\n", urb->status);
		goto resubmit_urb;
	}

	if (likely(urb->actual_length >= 0))
		sc_usb_process_rx_buffer(ch, (u8*)urb->transfer_buffer, (unsigned)urb->actual_length);


resubmit_urb:
	udev = interface_to_usbdev(ch->usb->intf);
	usb_fill_bulk_urb(
		urb, udev, usb_rcvbulkpipe(udev, ch->msg_epp),
		urb->transfer_buffer, ch->usb->msg_buffer_size,
		&sc_usb_urb_completed, ch);

	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (unlikely(rc)) {
		switch (rc) {
		case -ENODEV:
			// oopsie daisy, USB device gone, remove can dev
			netif_device_detach(ch->net);
			break;
		default:
			dev_err(&ch->usb->intf->dev, "URB resubmit failed: %d\n", rc);
			break;
		}
	}
}


static int sc_usb_start_continous_reader(struct sc_can_channel_data *ch)
{
	int rc = 0;
	unsigned i;
	struct usb_device *udev = interface_to_usbdev(ch->usb->intf);
	unsigned rx_pipe = usb_rcvbulkpipe(udev, ch->msg_epp);
	size_t bytes = ch->usb->msg_buffer_size;
// FIX ME!!!
	for (i = 0; i < SC_MAX_RX_URBS; ++i) {
		dma_addr_t dma;
		void* mem;
		struct urb *urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			goto check_urbs;
		}

		mem = usb_alloc_coherent(udev, bytes, GFP_KERNEL, &dma);
		if (!mem) {
			usb_free_urb(urb);
			goto check_urbs;
		}

		usb_fill_bulk_urb(urb, udev, rx_pipe, mem, bytes, &sc_usb_urb_completed, ch);
		urb->transfer_dma = dma;
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
		usb_anchor_urb(urb, &ch->rx_anchor);

		rc = usb_submit_urb(urb, GFP_KERNEL);
		if (rc) {
			usb_unanchor_urb(urb);
			usb_free_coherent(udev, bytes, mem, dma);
			usb_free_urb(urb);
			break;
		}

		// dev->rxbuf[i] = buf;
		// dev->rxbuf_dma[i] = buf_dma;

		usb_free_urb(urb);
	}

out:
	return rc;

check_urbs:
	if (!i) {
		dev_err(&ch->usb->intf->dev, "URB allocation failed\n");
		if (!rc)
			rc = -ENOMEM;
	} else if (i < SC_MAX_RX_URBS)
		dev_warn(&ch->usb->intf->dev, "only %u/%u rx URBs allocated\n", i, (unsigned)SC_MAX_RX_URBS);


	goto out;
}

static int sc_usb_apply_configuration(struct sc_can_channel_data *ch)
{
	return 0;
}

static int sc_can_open(struct net_device *dev)
{
	struct sc_net_device_data *net_data = netdev_priv(dev);
	struct sc_can_channel_data *ch = net_data->ch;
	int rc = 0;

	rc = open_candev(dev);
	if (rc)
		goto fail;

	rc = sc_usb_start_continous_reader(ch);
	if (rc)
		goto fail;

	rc = sc_usb_apply_configuration(ch);
	if (rc)
		goto fail;

	net_data->can.state = CAN_STATE_ERROR_ACTIVE;

out:
	return rc;

fail:
	sc_can_stop(dev);
	goto out;
}

static netdev_tx_t sc_can_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	return NETDEV_TX_OK;
}

static int sc_can_set_bittiming(struct net_device *dev)
{
	struct sc_net_device_data *net_data = netdev_priv(dev);
	struct sc_can_channel_data *ch = net_data->ch;

	return 0;
}

static int sc_can_set_data_bittiming(struct net_device *dev)
{
	return sc_can_set_bittiming(dev);
}

static int sc_can_set_mode(struct net_device *dev, enum can_mode mode)
{
	return 0;
}

static int sc_get_berr_counter(const struct net_device *dev, struct can_berr_counter *bec)
{
	struct sc_net_device_data *net_data = netdev_priv(dev);
	*bec = net_data->ch->bec;
	return 0;
}

static const struct net_device_ops sc_can_netdev_ops = {
	.ndo_open = &sc_can_open,
	.ndo_stop = &sc_can_stop,
	.ndo_start_xmit = &sc_can_start_xmit,
	.ndo_change_mtu = &can_change_mtu,
};


static void sc_can_chan_uninit(struct sc_can_channel_data *ch)
{
	struct sc_net_device_data *net_data;

	if (ch->net) {
		net_data = netdev_priv(ch->net);
		if (net_data->registered) {
			unregister_candev(ch->net);
		}
		free_candev(ch->net);
		ch->net = NULL;
	}


}

static int sc_can_chan_init(struct sc_can_channel_data *ch, struct sc_chan_info *info)
{
	int rc = 0;
	struct sc_net_device_data *net_data = NULL;

	BUG_ON(!ch);
	BUG_ON(!ch->usb);
	BUG_ON(!info);

	ch->cmd_epp = info->cmd_epp;
	ch->msg_epp = info->msg_epp;
	init_usb_anchor(&ch->rx_anchor);

	ch->net = alloc_candev(sizeof(*net_data), SC_MAX_TX_URBS);
	if (!ch->net) {
		dev_err(&ch->usb->intf->dev, "candev alloc failed\n");
		rc = -ENOMEM;
		goto fail;
	}

	net_data = netdev_priv(ch->net);
	net_data->ch = ch;

	net_data->can.ctrlmode_static = ch->usb->ctrlmode_static;
	net_data->can.ctrlmode_supported = ch->usb->ctrlmode_supported;
	net_data->can.state = CAN_STATE_STOPPED;
	net_data->can.clock.freq = ch->usb->can_clock_hz;

	net_data->can.do_set_mode = &sc_can_set_mode;
	net_data->can.do_get_berr_counter = &sc_get_berr_counter;
	net_data->can.bittiming_const = &ch->usb->nominal;
	net_data->can.do_set_bittiming = &sc_can_set_bittiming;

	if (ch->usb->features & SC_FEATURE_FLAG_FDF) {
		net_data->can.data_bittiming_const = &ch->usb->data;
		net_data->can.do_set_data_bittiming = &sc_can_set_data_bittiming;
	}

	ch->net->netdev_ops = &sc_can_netdev_ops;
	SET_NETDEV_DEV(ch->net, &ch->usb->intf->dev);

	rc = register_candev(ch->net);
	if (rc) {
		dev_err(&ch->usb->intf->dev, "candev registration failed\n");
		goto fail;
	}

	net_data->registered = 1;
	netdev_dbg(ch->net, "candev registration success\n");

out:
	return rc;

fail:
	sc_can_chan_uninit(ch);
	goto out;
}


static void sc_usb_cleanup(struct sc_usb_device_data *device_data)
{
	unsigned i;

	if (device_data) {
		if (device_data->chan_ptr) {
			for (i = 0; i < device_data->chan_count; ++i)
				sc_can_chan_uninit(&device_data->chan_ptr[i]);

			kfree(device_data->chan_ptr);
		}

		kfree(device_data);
	}
}

static void sc_usb_disconnect(struct usb_interface *intf)
{
	struct sc_usb_device_data *device_data = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (device_data)
		sc_usb_cleanup(device_data);
}

static int sc_usb_probe_prelim(struct usb_interface *intf, u8 *cmd_epp, u16 *ep_size)
{
	// ensure interface is set
	if (unlikely(!intf->cur_altsetting)) {
		return -ENODEV;
	}

	// we want a VENDOR interface
	if (intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC) {
		dev_dbg(&intf->dev, "not a vendor interface: %#02x\n", intf->cur_altsetting->desc.bInterfaceClass);
		return -ENODEV;
	}

	// must have at least one endpoint pair
	dev_dbg(&intf->dev, "device has %u eps\n", intf->cur_altsetting->desc.bNumEndpoints);
	if (unlikely(!(intf->cur_altsetting->desc.bNumEndpoints / 2))) {
		return -ENODEV;
	}

	// endpoint must be bulk
	if (unlikely(usb_endpoint_type(&intf->cur_altsetting->endpoint->desc) != USB_ENDPOINT_XFER_BULK)) {
		return -ENODEV;
	}

	*ep_size = le16_to_cpu(intf->cur_altsetting->endpoint->desc.wMaxPacketSize);
	dev_dbg(&intf->dev, "ep size %u\n", *ep_size);

	// endpoint too small?
	if (unlikely(*ep_size < SC_MIN_SUPPORTED_TRANSFER_SIZE)) {
		return -ENODEV;
	}

	*cmd_epp = usb_endpoint_num(&intf->cur_altsetting->endpoint->desc);
	dev_dbg(&intf->dev, "ep num %u\n", *cmd_epp);

	return 0;
}


static int sc_usb_probe_dev(struct sc_usb_device_data *device_data, u8 cmd_epp, u16 ep_size)
{
	int rc = 0;
	int actual_len;
	struct sc_msg_req *req;
	struct sc_msg_hello *device_hello;
	struct sc_msg_dev_info *device_info;
	struct sc_msg_can_info *can_info;
	void *tx_buf = NULL;
	void *rx_buf = NULL;
	unsigned i;
	char serial_str[1 + sizeof(device_info->sn_bytes)*2];
	char name_str[1 + sizeof(device_info->name_bytes)];
	struct usb_device *udev = interface_to_usbdev(device_data->intf);


	BUG_ON(!device_data);
	BUG_ON(!device_data->intf);
	BUG_ON(ep_size < SC_MIN_SUPPORTED_TRANSFER_SIZE);

	tx_buf = kmalloc(ep_size, GFP_KERNEL);
	if (!tx_buf) {
		rc = -ENOMEM;
		goto err;
	}

	rx_buf = kmalloc(ep_size, GFP_KERNEL);
	if (!rx_buf) {
		rc = -ENOMEM;
		goto err;
	}

	req = tx_buf;
	memset(req, 0, sizeof(*req));
	req->id = SC_MSG_HELLO_DEVICE;
	req->len = sizeof(*req);


	dev_dbg(&device_data->intf->dev, "sending SC_MSG_HELLO_DEVICE on %02x\n", cmd_epp);
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, cmd_epp), req,
			sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	/* wait for response */
	dev_dbg(&device_data->intf->dev, "waiting for SC_MSG_HELLO_HOST\n");
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

	device_data->cmd_buffer_size = ntohs(device_hello->cmd_buffer_size);
	device_data->msg_buffer_size = ntohs(device_hello->msg_buffer_size);

	if (0 == device_hello->proto_version
		|| device_data->cmd_buffer_size < SC_MIN_SUPPORTED_TRANSFER_SIZE
		|| device_data->msg_buffer_size < SC_MIN_SUPPORTED_TRANSFER_SIZE) {
		dev_err(&device_data->intf->dev,
			"badly configured device: proto_version=%u cmd_buffer_size=%u msg_buffer_size=%u\n",
			device_hello->proto_version, device_data->cmd_buffer_size, device_data->msg_buffer_size);
		rc = -ENODEV;
		goto err;
	}

	if (device_hello->proto_version > SC_VERSION) {
		dev_info(&device_data->intf->dev, "device proto version %u exceeds supported version %u\n",
			device_hello->proto_version, SC_VERSION);
		rc = -ENODEV;
		goto err;
	}

	/* At this point we are fairly confident we are dealing with the genuine article. */
	dev_info(
		&device_data->intf->dev,
		"device proto version %u, %s endian\n",
		device_hello->proto_version, device_hello->byte_order == SC_BYTE_ORDER_LE ? "little" : "big");

	if (SC_NATIVE_BYTE_ORDER == device_hello->byte_order) {
		device_data->host_to_dev16 = &sc_nop16;
		device_data->host_to_dev32 = &sc_nop32;
	} else {
		device_data->host_to_dev16 = &sc_swap16;
		device_data->host_to_dev32 = &sc_swap32;
	}

	// realloc cmd rx buffer if device reports larger cap
	if (ep_size < device_data->cmd_buffer_size)
		if (ksize(rx_buf) < device_data->cmd_buffer_size) {
			kfree(rx_buf);
			rx_buf = kmalloc(device_data->cmd_buffer_size, GFP_KERNEL);
			if (!rx_buf) {
				rc = -ENOMEM;
				goto err;
			}
		}

	req->id = SC_MSG_DEVICE_INFO;
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, cmd_epp), req, sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	/* wait for response */
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, cmd_epp), rx_buf, device_data->cmd_buffer_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	device_info = rx_buf;

	if (unlikely(actual_len < 0 ||
		(size_t)actual_len < sizeof(*device_info) ||
		SC_MSG_DEVICE_INFO != device_info->id ||
		device_info->len < sizeof(*device_info))) {
		dev_err(&device_data->intf->dev, "bad reply to SC_MSG_DEVICE_INFO (%d bytes)\n", actual_len);
		rc = -ENODEV;
		goto err;
	}

	for (i = 0; i < min((size_t)device_info->sn_len, ARRAY_SIZE(serial_str)-1); ++i)
		snprintf(&serial_str[i*2], 3, "%02x", device_info->sn_bytes[i]);

	serial_str[i*2] = 0;

	device_info->name_len = min((size_t)device_info->name_len, sizeof(name_str)-1);
	memcpy(name_str, device_info->name_bytes, device_info->name_len);
	name_str[device_info->name_len] = 0;

	dev_info(&device_data->intf->dev, "device %s, serial %s, firmware version %u.%u.%u\n",
		name_str, serial_str, device_info->fw_ver_major, device_info->fw_ver_minor, device_info->fw_ver_patch);


	req->id = SC_MSG_CAN_INFO;
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, cmd_epp), req, sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	/* wait for response */
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, cmd_epp), rx_buf, device_data->cmd_buffer_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	can_info = rx_buf;

	if (unlikely(actual_len < 0 ||
				 (size_t)actual_len < sizeof(*can_info) ||
	             SC_MSG_CAN_INFO != can_info->id ||
	             can_info->len < sizeof(*can_info) + sizeof(struct sc_chan_info) * can_info->chan_count ||
	             (size_t)actual_len < sizeof(*can_info) + sizeof(struct sc_chan_info) * can_info->chan_count)) {
		dev_err(&device_data->intf->dev, "bad reply to SC_MSG_CAN_INFO (%d bytes)\n", actual_len);
		rc = -ENODEV;
		goto err;
	}

	device_data->chan_count = can_info->chan_count;
	device_data->can_clock_hz = device_data->host_to_dev32(can_info->can_clk_hz);
	device_data->features = device_data->host_to_dev16(can_info->features);

	// nominal bitrate
	strlcpy(device_data->nominal.name, SC_NAME, sizeof(device_data->nominal.name));
	device_data->nominal.tseg1_min = can_info->nmbt_tseg1_min;
	device_data->nominal.tseg1_max = device_data->host_to_dev16(can_info->nmbt_tseg1_max);
	device_data->nominal.tseg2_min = can_info->nmbt_tseg2_min;
	device_data->nominal.tseg2_max = can_info->nmbt_tseg2_max;
	device_data->nominal.sjw_max = can_info->nmbt_sjw_max;
	device_data->nominal.brp_min = can_info->nmbt_brp_min;
	device_data->nominal.brp_max = device_data->host_to_dev16(can_info->nmbt_brp_max);
	device_data->nominal.brp_inc = 1;

	// data bitrate
	strlcpy(device_data->data.name, SC_NAME, sizeof(device_data->data.name));
	device_data->data.tseg1_min = can_info->dtbt_tseg1_min;
	device_data->data.tseg1_max = can_info->dtbt_tseg1_max;
	device_data->data.tseg2_min = can_info->dtbt_tseg2_min;
	device_data->data.tseg2_max = can_info->dtbt_tseg2_max;
	device_data->data.sjw_max = can_info->dtbt_sjw_max;
	device_data->data.brp_min = can_info->dtbt_brp_min;
	device_data->data.brp_max = can_info->dtbt_brp_max;
	device_data->data.brp_inc = 1;

	device_data->ctrlmode_static = CAN_CTRLMODE_BERR_REPORTING;

	if (device_data->features & SC_FEATURE_FLAG_FDF) {
		device_data->ctrlmode_supported |= CAN_CTRLMODE_FD;
	}

	if (device_data->features & SC_FEATURE_FLAG_MON_MODE) {
		device_data->ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;
	}

	if (device_data->features & SC_FEATURE_FLAG_EXT_LOOP_MODE) {
		device_data->ctrlmode_supported |= CAN_CTRLMODE_LOOPBACK;
	}


	device_data->chan_ptr = kcalloc(device_data->chan_count, sizeof(*device_data->chan_ptr), GFP_KERNEL);
	if (!device_data->chan_ptr) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < device_data->chan_count; ++i) {
		device_data->chan_ptr[i].usb = device_data;
		rc = sc_can_chan_init(&device_data->chan_ptr[i], &can_info->chan_info[i]);
		if (rc)
			goto err;
	}

cleanup:
	kfree(tx_buf);
	kfree(rx_buf);

	return rc;

err:
	goto cleanup;
}

static int sc_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	int rc = 0;
	u8 cmd_epp;
	u16 ep_size;
	struct sc_usb_device_data *device_data = NULL;

	(void)id; // unused

	rc = sc_usb_probe_prelim(intf, &cmd_epp, &ep_size);
	if (rc)
		goto err;

	device_data = kzalloc(sizeof(*device_data), GFP_KERNEL);
	if (!device_data) {
		rc = -ENOMEM;
		goto err;
	}

	device_data->intf = intf;

	rc = sc_usb_probe_dev(device_data, cmd_epp, ep_size);
	if (rc)
		goto err;

	usb_set_intfdata(intf, device_data);

out:
	return rc;

err:
	sc_usb_cleanup(device_data);
	goto out;
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