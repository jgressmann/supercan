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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

#include <linux/can.h>
#include <linux/can/dev.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/usb.h>

#pragma GCC diagnostic pop
#pragma GCC diagnostic error "-Wall"
#pragma GCC diagnostic error "-Wextra"



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
#define SC_MAX_RX_URBS 128
#define SC_MAX_TX_URBS 128

#define SC_TX_URB_FLAG_TX_BACK  0x1
#define SC_TX_URB_FLAG_TXR_BACK 0x2



struct sc_chan;
struct sc_net_priv {
	struct can_priv can; /* must be the first member */
	struct sc_chan *ch;
};

struct sc_urb_data {
	struct sc_chan *ch;
	struct urb *urb;
	void *mem;
	dma_addr_t dma_addr;
	u8 flags;
	u8 len;
};

struct sc_usb_priv;
struct sc_chan {
	struct sc_usb_priv *usb_priv;   /* filled in by sc_usb_probe */
	struct net_device *netdev;
	struct can_berr_counter bec;
	struct sc_urb_data *rx_urb_ptr;
	struct sc_urb_data *tx_urb_ptr;
	u8 *tx_cmd_buffer;
	u8 *rx_cmd_buffer;              /* points into tx_cmd_buffer mem, don't free */
	u8 *tx_urb_available_ptr;
	ktime_t device_time_now;
	spinlock_t tx_lock;
	u32 device_time_base;
	u8 cmd_epp;                     /* OBSOLETE */
	u8 msg_epp;                     /* OBSOLETE */
	u8 index;                       /* filled in by sc_usb_probe */
	u8 registered;
	u8 rx_urb_count;
	u8 tx_urb_count;
	u8 tx_urb_available_count;
	u8 device_time_received;
	u8 ready;
	u8 prev_rx_fifo_size;
	u8 prev_tx_fifo_size;
};

/* usb interface struct */
struct sc_usb_priv {
	struct sc_chan *chan_ptr;
	struct usb_interface *intf;
	u16 (*host_to_dev16)(u16);
	u32 (*host_to_dev32)(u32);
	u32 can_clock_hz;
	u32 ctrlmode_static;
	u32 ctrlmode_supported;
	struct can_bittiming_const nominal;
	struct can_bittiming_const data;
	u16 feat_perm;
	u16 feat_conf;
	u16 cmd_buffer_size;
	u16 msg_buffer_size;
	u8 cmd_epp;
	u8 msg_epp;
	u8 chan_count;                      /* OBSOLETE, always 1 */
	u8 tx_fifo_size;
	u8 rx_fifo_size;
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

static int sc_usb_map_error(int error)
{
	switch (error) {
	case SC_ERROR_UNKNOWN:
		return -ENODEV;
	case SC_ERROR_NONE:
		return 0;
	case SC_ERROR_SHORT:
		return -ETOOSMALL;
	case SC_ERROR_PARAM:
		return -EINVAL;
	case SC_ERROR_BUSY:
		return -EBUSY;
	case SC_ERROR_UNSUPPORTED:
		return -ENOTSUPP;
	default:
		return -ENODEV;
	}
}

static inline u8 sc_usb_map_proto_error_type(u8 error)
{
	switch (error) {
	case SC_CAN_ERROR_NONE: return CAN_ERR_PROT_UNSPEC;
	case SC_CAN_ERROR_STUFF: return CAN_ERR_PROT_STUFF;
	case SC_CAN_ERROR_FORM: return CAN_ERR_PROT_FORM;
	case SC_CAN_ERROR_ACK: return CAN_ERR_PROT_UNSPEC;
	case SC_CAN_ERROR_BIT1: return CAN_ERR_PROT_BIT;
	case SC_CAN_ERROR_BIT0: return CAN_ERR_PROT_BIT;
	case SC_CAN_ERROR_CRC: return CAN_ERR_PROT_UNSPEC;
	default: return CAN_ERR_PROT_UNSPEC;
	}
}

static inline bool sc_usb_tx_urb_done(struct sc_urb_data const *urb_data)
{
	const u8 done_flags = SC_TX_URB_FLAG_TX_BACK | SC_TX_URB_FLAG_TXR_BACK;
	return (urb_data->flags & done_flags) == done_flags;
}

static inline void sc_can_ch_ktime_from_us(
	struct sc_chan *ch, u32 timestamp_us, ktime_t *duration)
{
	ktime_t result = 0;
	const u32 HALF_TIME = __UINT32_MAX__ / 2;
	u32 diff_us = 0;

	// netdev_dbg(ch->netdev, "recv=%u\n", ch->device_time_received);

	if (likely(ch->device_time_received)) {
		// netdev_dbg(ch->netdev, "ts=%lu\n", (unsigned long)timestamp_us);

		diff_us = timestamp_us - ch->device_time_base;

		if (diff_us >= HALF_TIME) {
			/* delayed */
			diff_us = ch->device_time_base - timestamp_us;
			// netdev_dbg(ch->netdev, "delayed ts=%u base=%u diff=%u\n", timestamp_us, ch->device_time_base, diff_us);
			result = ktime_sub_us(ch->device_time_now, diff_us);
		} else {
			/* wrap around / track */
			// netdev_dbg(ch->netdev, "%s ts=%u base=%u diff=%u\n", (timestamp_us < ch->device_time_base ? "wrap" : "track"), timestamp_us, ch->device_time_base, diff_us);
			ch->device_time_base = timestamp_us;
			ch->device_time_now = ktime_add_us(ch->device_time_now, diff_us);
			result = ch->device_time_now;
		}
	} else {
		netdev_dbg(ch->netdev, "init ts=%lu\n", (unsigned long)timestamp_us);
		ch->device_time_received = 1;
		ch->device_time_base = timestamp_us;
		ch->device_time_now = ktime_set(0, 0);
		result = ch->device_time_now;
	}

	*duration = result;
}

static inline void sc_can_ch_update_ktime_from_us(struct sc_chan *ch, u32 timestamp_us)
{
	ktime_t unused;
	sc_can_ch_ktime_from_us(ch, timestamp_us, &unused);
}

static int sc_usb_cmd_send_receive(
	struct sc_usb_priv *priv, u8 ep, void *tx_ptr, int tx_len, void *rx_ptr)
{
	struct usb_device *udev = interface_to_usbdev(priv->intf);
	struct sc_msg_error *error_msg = rx_ptr;
	int rc = 0;
	int tx_actual_len = 0, rx_actual_len = 0;

	/* send */
	dev_dbg(&priv->intf->dev, "send %d bytes on ep %u\n", tx_len, ep);
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, ep), tx_ptr, tx_len, &tx_actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		return rc;

	if (unlikely(tx_actual_len != tx_len))
		return -EPIPE;

	/* wait for response */
	dev_dbg(&priv->intf->dev, "wait for data on ep %u\n", ep);
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, ep), rx_ptr, priv->cmd_buffer_size, &rx_actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		return rc;

	/* check for short reply */
	if (unlikely(
		rx_actual_len < 0 ||
		(size_t)rx_actual_len < sizeof(*error_msg))) {

		dev_err(&priv->intf->dev, "short reply\n");
		return -ETOOSMALL;
	}

	/* check for proper message id */
	if (unlikely(error_msg->id != SC_MSG_ERROR)) {
		dev_err(&priv->intf->dev,
			"unexpected response id %#02x expected %#02x (SC_MSG_ERROR)\n",
			error_msg->id, SC_MSG_ERROR);
		return -EPROTO;
	}

	dev_dbg(&priv->intf->dev, "response received, error code %d\n", error_msg->error);

	return sc_usb_map_error(error_msg->error);
}

static inline int sc_can_ch_cmd_send_receive(struct sc_chan *ch, unsigned cmd_len)
{
	BUG_ON(!ch);
	BUG_ON(!cmd_len);
	BUG_ON(cmd_len > ch->usb_priv->cmd_buffer_size);
	return sc_usb_cmd_send_receive(ch->usb_priv, ch->cmd_epp, ch->tx_cmd_buffer, cmd_len, ch->rx_cmd_buffer);
}

static int sc_can_close(struct net_device *netdev)
{
	struct sc_net_priv *priv = netdev_priv(netdev);
	struct sc_chan *ch = priv->ch;
	struct sc_msg_config *bus_off = (struct sc_msg_config *)ch->tx_cmd_buffer;
	int rc = 0;
	unsigned i = 0;

	netif_stop_queue(netdev);

	netdev_dbg(netdev, "close device\n");
	(void)close_candev(netdev);
	netdev_dbg(netdev, "device closed\n");
	priv->can.state = CAN_STATE_STOPPED;

	memset(bus_off, 0, sizeof(*bus_off));
	bus_off->id = SC_MSG_BUS;
	bus_off->len = sizeof(*bus_off);
	rc = sc_can_ch_cmd_send_receive(ch, bus_off->len);
	if (rc)
		netdev_dbg(netdev, "failed to go off bus (%d)\n", rc);

	for (i = 0; i < ch->rx_urb_count; ++i) {
		struct sc_urb_data *urb_data = &ch->rx_urb_ptr[i];
		usb_kill_urb(urb_data->urb);
	}

	for (i = 0; i < ch->tx_urb_count; ++i) {
		struct sc_urb_data *urb_data = &ch->tx_urb_ptr[i];
		usb_kill_urb(urb_data->urb);
	}

	// reset tx urb flags, fill available list
	for (i = 0; i < ch->tx_urb_count; ++i) {
		struct sc_urb_data *urb_data = &ch->tx_urb_ptr[i];
		urb_data->flags = 0;
		ch->tx_urb_available_ptr[i] = i;
	}
	WRITE_ONCE(ch->tx_urb_available_count, ch->tx_urb_count);


	// reset time
	ch->device_time_base = 0;
	ch->device_time_received = 0;


	WRITE_ONCE(ch->ready, 0);

	ch->prev_rx_fifo_size = 0;
	ch->prev_tx_fifo_size = 0;

	return 0;
}


static int sc_usb_process_can_error(struct sc_chan *ch, struct sc_msg_can_error *error)
{
	struct net_device *netdev = ch->netdev;
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct net_device_stats *net_stats = &netdev->stats;
	struct can_device_stats *can_stats = &net_priv->can.can_stats;
	struct sk_buff *skb = NULL;
	struct can_frame *cf = NULL;

if (unlikely(error->len < sizeof(*error))) {
		netdev_err(netdev, "short sc_msg_can_error (%u)\n", error->len);
		return -ETOOSMALL;
	}

	skb = alloc_can_err_skb(netdev, &cf);
	if (unlikely(!skb))
		return -ENOMEM;


	if (SC_CAN_ERROR_ACK == error->error)
		cf->can_id |= CAN_ERR_ACK | CAN_ERR_BUSERROR;
	else {
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;
		cf->data[2] |= sc_usb_map_proto_error_type(error->error);
		if (error->flags & SC_CAN_ERROR_FLAG_RXTX_TX)
			cf->data[2] |= CAN_ERR_PROT_TX;
	}

	++can_stats->bus_error;

	if (net_ratelimit())
		netdev_dbg(netdev, "error frame %08x: %02x%02x%02x%02x%02x%02x%02x%02x\n",
			cf->can_id, cf->data[0], cf->data[1], cf->data[2], cf->data[3],
			cf->data[4], cf->data[5], cf->data[6], cf->data[7]);

	++net_stats->rx_packets;
	net_stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);

	return 0;
}

static int sc_usb_process_can_status(struct sc_chan *ch, struct sc_msg_can_status *status)
{
	struct net_device *netdev = ch->netdev;
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct net_device_stats *net_stats = &netdev->stats;
	struct can_device_stats *can_stats = &net_priv->can.can_stats;
	struct sk_buff *skb = NULL;
	struct can_frame *cf = NULL;
	struct can_frame f = {
		.can_id = CAN_ERR_FLAG,
		.can_dlc = CAN_ERR_DLC,
		.data = {0, 0, 0, 0, 0, 0, 0, 0},
	};
	enum can_state curr_state = 0, next_state = 0;
	u16 rx_lost = 0, tx_dropped = 0;

	if (unlikely(status->len < sizeof(*status))) {
		netdev_err(netdev, "short sc_msg_can_status (%u)\n", status->len);
		return -ETOOSMALL;
	}

	if (status->rx_fifo_size >= ch->usb_priv->rx_fifo_size / 2 &&
		status->rx_fifo_size != ch->prev_rx_fifo_size) {
		if (net_ratelimit())
			netdev_dbg(netdev, "rx fs=%u\n", status->rx_fifo_size);

		ch->prev_rx_fifo_size = status->rx_fifo_size;
	}

	if (status->tx_fifo_size >= ch->usb_priv->tx_fifo_size / 2 &&
		status->tx_fifo_size != ch->prev_tx_fifo_size) {
		if (net_ratelimit())
			netdev_dbg(netdev, "tx fs=%u\n", status->tx_fifo_size);

		ch->prev_tx_fifo_size = status->tx_fifo_size;
	}
	// if (status->tx_fifo_size != ch->prev_tx_fifo_size) {
	// 	if (net_ratelimit())
	// 		netdev_dbg(netdev, "tx fs=%u\n", status->tx_fifo_size);

	// 	ch->prev_tx_fifo_size = status->tx_fifo_size;
	// }

	sc_can_ch_update_ktime_from_us(ch, ch->usb_priv->host_to_dev32(status->timestamp_us));

	rx_lost = ch->usb_priv->host_to_dev16(status->rx_lost);
	tx_dropped = ch->usb_priv->host_to_dev16(status->tx_dropped);

	if (unlikely(rx_lost)) {
		if (net_ratelimit())
			netdev_dbg(netdev, "rx lost %u frames\n", rx_lost);
		net_stats->rx_over_errors += rx_lost;
		f.can_id |= CAN_ERR_CRTL;
		f.data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
	}

	if (unlikely(tx_dropped)) {
		if (net_ratelimit())
			netdev_dbg(netdev, "tx dropped %u frames\n", tx_dropped);
		net_stats->tx_dropped += tx_dropped;
		f.can_id |= CAN_ERR_CRTL;
		f.data[1] |= CAN_ERR_CRTL_TX_OVERFLOW;
	}

	f.data[5] = status->rx_errors;
	f.data[6] = status->tx_errors;

	ch->bec.rxerr = status->rx_errors;
	ch->bec.txerr = status->tx_errors;


	curr_state = net_priv->can.state;

	if (status->flags & SC_CAN_STATUS_FLAG_TXR_DESYNC) {
		/* go bus off. restarting the bus will clear desync */
		next_state = CAN_STATE_BUS_OFF;
		if (net_ratelimit())
			netdev_err(netdev, "txr desync\n");
	} else if (SC_CAN_STATUS_BUS_OFF == status->bus_status) {
		++can_stats->bus_off;
		next_state = CAN_STATE_BUS_OFF;
	} else if (SC_CAN_STATUS_ERROR_PASSIVE == status->bus_status) {
		++can_stats->error_passive;
		next_state = CAN_STATE_ERROR_PASSIVE;
	} else if (SC_CAN_STATUS_ERROR_WARNING == status->bus_status) {
		++can_stats->error_warning;
		next_state = CAN_STATE_ERROR_WARNING;
	} else {
		next_state = CAN_STATE_ERROR_ACTIVE;
	}

	if (next_state != curr_state) {
		netdev_info(netdev, "can bus status 0x%02x -> 0x%02x\n", curr_state, next_state);
		can_change_state(netdev, &f, next_state, next_state);
		if (CAN_STATE_BUS_OFF == next_state) {
			can_bus_off(netdev);
		}
	}

	/* generate error frame */
	if (f.can_id != CAN_ERR_FLAG) {
		skb = alloc_can_err_skb(netdev, &cf);
		if (unlikely(!skb))
			return -ENOMEM;

		*cf = f;

		if (net_ratelimit())
			netdev_dbg(netdev, "error frame %08x: %02x%02x%02x%02x%02x%02x%02x%02x\n",
				cf->can_id, cf->data[0], cf->data[1], cf->data[2], cf->data[3],
				cf->data[4], cf->data[5], cf->data[6], cf->data[7]);

		++net_stats->rx_packets;
		net_stats->rx_bytes += cf->can_dlc;
		netif_rx(skb);
	}

	return 0;
}

static int sc_usb_process_can_rx(struct sc_chan *ch, struct sc_msg_can_rx *rx)
{
	struct net_device *netdev = ch->netdev;
	struct net_device_stats *net_stats = &netdev->stats;
	struct sk_buff *skb;
	unsigned data_len;
	canid_t can_id;
	u32 timestamp_us;

	if (unlikely(rx->len < sizeof(*rx))) {
		dev_err(
			&ch->usb_priv->intf->dev,
			"short sc_msg_can_rx (%u)\n",
			rx->len);
		return -ETOOSMALL;
	}

	data_len = can_dlc2len(rx->dlc);
	can_id = ch->usb_priv->host_to_dev32(rx->can_id);

	if (rx->flags & SC_CAN_FRAME_FLAG_RTR) {
		can_id |= CAN_RTR_FLAG;
	} else {
		if (unlikely(rx->len < sizeof(*rx) + data_len)) {
			dev_err(
				&ch->usb_priv->intf->dev,
				"short sc_msg_can_rx (%u)\n",
				rx->len);
			return -ETOOSMALL;
		}
	}

	timestamp_us = ch->usb_priv->host_to_dev32(rx->timestamp_us);


	if (rx->flags & SC_CAN_FRAME_FLAG_EXT) {
		can_id |= CAN_EFF_FLAG;
	}

	if (rx->flags & SC_CAN_FRAME_FLAG_FDF) {
		struct canfd_frame *cf;

		skb = alloc_canfd_skb(netdev, &cf);
		if (!skb) {
			++net_stats->rx_dropped;
			return 0;
		}

		cf->can_id = can_id;
		cf->len = data_len;

		if (rx->flags & SC_CAN_FRAME_FLAG_BRS) {
			cf->flags |= CANFD_BRS;
		}

		if (rx->flags & SC_CAN_FRAME_FLAG_ESI) {
			cf->flags |= CANFD_ESI;
		}

		if (!(rx->flags & SC_CAN_FRAME_FLAG_RTR) && data_len) {
			memcpy(cf->data, rx->data, data_len);
			netdev->stats.rx_bytes += data_len;
		}
	} else {
		struct can_frame *cf;
		skb = alloc_can_skb(netdev, &cf);
		if (!skb) {
			net_stats->rx_dropped++;
			return 0;
		}

		cf->can_id = can_id;
		cf->can_dlc = data_len;

		if (!(rx->flags & SC_CAN_FRAME_FLAG_RTR) && data_len) {
			memcpy(cf->data, rx->data, data_len);
			netdev->stats.rx_bytes += data_len;
		}
	}

	++netdev->stats.rx_packets;

	sc_can_ch_ktime_from_us(ch, timestamp_us, &skb_hwtstamps(skb)->hwtstamp);

	netif_rx(skb);

	return 0;
}

static inline void sc_usb_tx_return_urb_unsafe(struct sc_urb_data *urb_data, bool *wake_netdev)
{
	struct sc_chan *ch = urb_data->ch;
	const unsigned index = urb_data - ch->tx_urb_ptr;
#if DEBUG
	unsigned i;
#endif
	BUG_ON(!sc_usb_tx_urb_done(urb_data));

	urb_data->flags = 0;

	BUG_ON(ch->tx_urb_available_count > ch->tx_urb_count);
	BUG_ON(index >= ch->tx_urb_count);
#if DEBUG
	for (i = 0; i < ch->tx_urb_available_count; ++i) {
		BUG_ON(index == ch->tx_urb_available_ptr[i]);
	}
#endif
	*wake_netdev = !ch->tx_urb_available_count;
	ch->tx_urb_available_ptr[ch->tx_urb_available_count++] = index;

	// if (net_ratelimit())
	// 	netdev_dbg(ch->netdev, "returned URB index %u\n", index);
}

static int sc_usb_process_can_txr(struct sc_chan *ch, struct sc_msg_can_txr *txr)
{
	struct net_device *netdev = ch->netdev;
	struct sc_urb_data *urb_data = NULL;
	unsigned long flags = 0;
	unsigned index = 0;
	u8 len = 0;
	bool wake = false;

	if (unlikely(txr->len < sizeof(*txr))) {
		netdev_warn(netdev, "short sc_msg_can_txr (%u)\n", txr->len);
		return -ETOOSMALL;
	}

	index = txr->track_id;

	if (index >= ch->tx_urb_count) {
		netdev_warn(netdev, "out of bounds txr id %u (max %u)\n", index, ch->tx_urb_count);
		return -ERANGE;
	}

	// netdev_dbg(netdev, "txr id %u %s\n", index, (txr->flags & SC_CAN_FRAME_FLAG_DRP) ? "dropped" : "transmitted");

	sc_can_ch_update_ktime_from_us(ch, ch->usb_priv->host_to_dev32(txr->timestamp_us));


	spin_lock_irqsave(&ch->tx_lock, flags);
	BUG_ON(index >= ch->tx_urb_count);
	urb_data = &ch->tx_urb_ptr[index];
	BUG_ON(urb_data->flags & SC_TX_URB_FLAG_TXR_BACK);
	urb_data->flags |= SC_TX_URB_FLAG_TXR_BACK;
	len = urb_data->len;
	if (sc_usb_tx_urb_done(urb_data)) {
		sc_usb_tx_return_urb_unsafe(urb_data, &wake);
		if (wake)
			netif_wake_queue(netdev);
	}

	// place / remove echo buffer
	if (txr->flags & SC_CAN_FRAME_FLAG_DRP) {
		++netdev->stats.tx_dropped;
		can_free_echo_skb(netdev, index);
	} else {
		++netdev->stats.tx_packets;
		netdev->stats.tx_bytes += len;
		can_get_echo_skb(netdev, index);
	}
	spin_unlock_irqrestore(&ch->tx_lock, flags);

	return 0;
}

static int sc_usb_process_msg(struct sc_chan *ch, struct sc_msg_header *hdr)
{
	switch (hdr->id) {
	case SC_MSG_CAN_STATUS:
		return sc_usb_process_can_status(ch, (struct sc_msg_can_status *)hdr);
	case SC_MSG_CAN_ERROR:
		return sc_usb_process_can_error(ch, (struct sc_msg_can_error *)hdr);
	case SC_MSG_CAN_RX:
		return sc_usb_process_can_rx(ch, (struct sc_msg_can_rx *)hdr);
		break;
	case SC_MSG_CAN_TXR:
		return sc_usb_process_can_txr(ch, (struct sc_msg_can_txr *)hdr);
	default:
		netdev_dbg(ch->netdev, "skip unknown msg id %#02x len %u\n", hdr->id, hdr->len);
		/* unhandled messages are expected as the protocol evolves */
		return -EINVAL;
	}
}

static void sc_usb_process_rx_buffer(struct sc_chan *ch, u8 *ptr, unsigned size)
{
	int error = 0;
	u8 * const sptr = ptr;
	u8 * const eptr = ptr + size;

	while (ptr + SC_MSG_HEADER_LEN <= eptr) {
		struct sc_msg_header *hdr = (struct sc_msg_header *)ptr;

		if (SC_MSG_EOF == hdr->id || !hdr->len) {
			ptr = eptr;
			break;
		}

		if (unlikely(ptr + hdr->len > eptr)) {
			dev_err_ratelimited(
				&ch->usb_priv->intf->dev,
				"short msg: offset=%u id=%02x, remaining %u bytes will be skipped\n",
				(unsigned)(ptr - sptr), hdr->id, (unsigned)(eptr-ptr));
			error = -ETOOSMALL;
			break;
		}

		ptr += hdr->len;

		error = sc_usb_process_msg(ch, hdr);
		if (unlikely(error)) {
			break;
		}
	}

	if (unlikely(error || ptr < eptr)) {
		netdev_info(ch->netdev, "rx msg dump (%u bytes)\n", size);
		print_hex_dump(KERN_INFO, SC_NAME " ", DUMP_PREFIX_NONE, 16, 1, sptr, size, false);
	}
}

static void sc_usb_tx_completed(struct urb *urb)
{
	struct sc_urb_data *urb_data = NULL;
	struct sc_chan *ch = NULL;
	unsigned long flags = 0;
	unsigned index = 0;
	bool wake = false;

	BUG_ON(!urb);
	BUG_ON(!urb->context);

	urb_data = (struct sc_urb_data *)urb->context;
	BUG_ON(urb_data->flags & SC_TX_URB_FLAG_TX_BACK);
	urb_data->flags |= SC_TX_URB_FLAG_TX_BACK;
	ch = urb_data->ch;
	index = urb_data - ch->tx_urb_ptr;
	// netdev_dbg(ch->netdev, "tx URB index %u back\n", index);
	BUG_ON(index >= ch->tx_urb_count);
	(void)index;

	spin_lock_irqsave(&ch->tx_lock, flags);
	if (sc_usb_tx_urb_done(urb_data)) {
		sc_usb_tx_return_urb_unsafe(urb_data, &wake);
	}

	if (0 == urb->status) {
		if (wake)
			netif_wake_queue(ch->netdev);
	} else {
		switch (urb->status) {
		case -ENOENT:
		case -EILSEQ:
		case -ESHUTDOWN:
		case -EPIPE:
		case -ETIME:
			break;
		default:
			netdev_info(ch->netdev, "tx URB status %d\n", urb->status);
			break;
		}
	}
	spin_unlock_irqrestore(&ch->tx_lock, flags);
}

static void sc_usb_rx_completed(struct urb *urb)
{
	struct sc_urb_data *urb_data = NULL;
	struct sc_chan *ch = NULL;
	int rc = 0;
	unsigned ready = 0;

	BUG_ON(!urb);
	BUG_ON(!urb->context);

	urb_data = (struct sc_urb_data *)urb->context;
	ch = urb_data->ch;

	if (likely(0 == urb->status)) {
		ready = READ_ONCE(ch->ready);
		if (likely(ready)) {
			if (likely(urb->actual_length > 0))
				sc_usb_process_rx_buffer(ch, (u8*)urb->transfer_buffer, (unsigned)urb->actual_length);
		}

		BUG_ON(urb->transfer_buffer_length != ch->usb_priv->msg_buffer_size);
		rc = usb_submit_urb(urb, GFP_ATOMIC);
		if (unlikely(rc)) {
			dev_dbg(&ch->usb_priv->intf->dev, "rx URB index %u\n", (unsigned)(urb_data - ch->rx_urb_ptr));
			switch (rc) {
			case -ENOENT:
			case -ETIME:
				// dev_dbg(&ch->usb_priv->intf->dev, "rx URB resubmit failed: %d\n", rc);
				break;
			case -ENODEV:
				// oopsie daisy, USB device gone, remove can dev
				netif_device_detach(ch->netdev);
				break;
			default:
				dev_err(&ch->usb_priv->intf->dev, "rx URB resubmit failed: %d\n", rc);
				break;
			}
		}
	} else {
		dev_dbg(&ch->usb_priv->intf->dev, "rx URB index %u\n", (unsigned)(urb_data - ch->rx_urb_ptr));
		switch (urb->status) {
		case -ENOENT:
		case -EILSEQ:
		case -ESHUTDOWN:
		case -EPIPE:
		case -ETIME:
			break;
		default:
			dev_info(&ch->usb_priv->intf->dev, "rx URB status %d\n", urb->status);
			break;
		}
	}
}



static int sc_usb_submit_rx_urbs(struct sc_chan *ch)
{
	int rc = 0;
	unsigned i = 0;


	BUG_ON(!ch);
	BUG_ON(ch->rx_urb_count > 0 && NULL == ch->rx_urb_ptr);

	for (i = 0; i < ch->rx_urb_count; ++i) {
		struct sc_urb_data *urb_data = &ch->rx_urb_ptr[i];
		rc = usb_submit_urb(urb_data->urb, GFP_KERNEL);
		if (unlikely(rc)) {
			netdev_dbg(ch->netdev, "rx URB submit index %u failed: %d\n", i, rc);
			break;
		} else {
			netdev_dbg(ch->netdev, "submit rx URB index %u\n", i);
		}
	}

	return rc;
}


static int sc_usb_apply_configuration(struct sc_chan *ch)
{
	struct sc_net_priv *net_priv = netdev_priv(ch->netdev);
	struct sc_msg_config *conf = (struct sc_msg_config *)ch->tx_cmd_buffer;
	struct sc_msg_features *feat = (struct sc_msg_features *)ch->tx_cmd_buffer;
	struct sc_msg_bittiming *bt = (struct sc_msg_bittiming *)ch->tx_cmd_buffer;
	int rc = 0;
	int can_fd_mode = (net_priv->can.ctrlmode & CAN_CTRLMODE_FD) == CAN_CTRLMODE_FD;
	int single_shot_mode = (net_priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT) == CAN_CTRLMODE_ONE_SHOT;
	u32 features = SC_FEATURE_FLAG_TXR |
			(can_fd_mode ? SC_FEATURE_FLAG_FDF : 0) | (single_shot_mode ? SC_FEATURE_FLAG_DAR : 0);

	// clear previous features
	memset(feat, 0, sizeof(*feat));
	feat->id = SC_MSG_FEATURES;
	feat->len = sizeof(*feat);
	feat->op = SC_FEAT_OP_CLEAR;

	netdev_dbg(ch->netdev, "clear features\n");
	rc = sc_can_ch_cmd_send_receive(ch, sizeof(*feat));
	if (rc)
		return rc;

	// set target features
	feat->op = SC_FEAT_OP_OR;
	feat->arg = ch->usb_priv->host_to_dev32(features);

	netdev_dbg(ch->netdev, "add features %#x\n", features);
	rc = sc_can_ch_cmd_send_receive(ch, sizeof(*feat));
	if (rc)
		return rc;

	// bittiming
	memset(bt, 0, sizeof(*bt));
	bt->id = SC_MSG_NM_BITTIMING;
	bt->len = sizeof(*bt);
	bt->brp = ch->usb_priv->host_to_dev16(net_priv->can.bittiming.brp);
	bt->sjw = net_priv->can.bittiming.sjw;
	bt->tseg1 = ch->usb_priv->host_to_dev16(net_priv->can.bittiming.prop_seg + net_priv->can.bittiming.phase_seg1);
	bt->tseg2 = net_priv->can.bittiming.phase_seg2;
	netdev_dbg(ch->netdev, "nominal brp=%lu sjw=%lu tseg1=%lu, tseg2=%lu bitrate=%lu\n",
		(unsigned long)net_priv->can.bittiming.brp, (unsigned long)net_priv->can.bittiming.sjw,
		(unsigned long)(net_priv->can.bittiming.prop_seg + net_priv->can.bittiming.phase_seg1),
		(unsigned long)net_priv->can.bittiming.phase_seg2, (unsigned long)net_priv->can.bittiming.bitrate);

	netdev_dbg(ch->netdev, "set nomimal bittiming\n");
	rc = sc_can_ch_cmd_send_receive(ch, sizeof(*bt));
	if (rc)
		return rc;

	if (can_fd_mode) {
		bt->id = SC_MSG_DT_BITTIMING;
		bt->brp = ch->usb_priv->host_to_dev16(net_priv->can.data_bittiming.brp);
		bt->sjw = net_priv->can.data_bittiming.sjw;
		bt->tseg1 = ch->usb_priv->host_to_dev16(net_priv->can.data_bittiming.prop_seg + net_priv->can.data_bittiming.phase_seg1);
		bt->tseg2 = net_priv->can.data_bittiming.phase_seg2;
		netdev_dbg(ch->netdev, "data brp=%lu sjw=%lu tseg1=%lu, tseg2=%lu bitrate=%lu\n",
			(unsigned long)net_priv->can.data_bittiming.brp, (unsigned long)net_priv->can.data_bittiming.sjw,
			(unsigned long)(net_priv->can.data_bittiming.prop_seg + net_priv->can.data_bittiming.phase_seg1),
			(unsigned long)net_priv->can.data_bittiming.phase_seg2, (unsigned long)net_priv->can.data_bittiming.bitrate);

		netdev_dbg(ch->netdev, "set data bittiming\n");
		rc = sc_can_ch_cmd_send_receive(ch, sizeof(*bt));
		if (rc)
			return rc;
	}

	// bus on
	memset(conf, 0, sizeof(*conf));
	conf->id = SC_MSG_BUS;
	conf->len = sizeof(*conf);
	conf->arg = ch->usb_priv->host_to_dev16(1);

	netdev_dbg(ch->netdev, "bus on\n");
	rc = sc_can_ch_cmd_send_receive(ch, sizeof(*conf));
	if (rc)
		return rc;


	return 0;
}

static int sc_can_open(struct net_device *netdev)
{
	struct sc_net_priv *priv = netdev_priv(netdev);
	int rc = 0;

	rc = open_candev(netdev);
	if (rc) {
		netdev_dbg(netdev, "candev open failed: %d\n", rc);
		goto fail;
	}

	rc = sc_usb_apply_configuration(priv->ch);
	if (rc) {
		netdev_dbg(netdev, "apply config failed: %d\n", rc);
		goto fail;
	}

	rc = sc_usb_submit_rx_urbs(priv->ch);
	if (rc) {
		netdev_dbg(netdev, "submit rx urbs failed: %d\n", rc);
		goto fail;
	}

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	netdev_dbg(netdev, "start queue\n");
	netif_start_queue(netdev);

	WRITE_ONCE(priv->ch->ready, 1);
out:
	return rc;

fail:
	netdev_dbg(netdev, "sc_can_open failed, closing device\n");
	sc_can_close(netdev);
	goto out;
}

static void sc_can_fill_tx_urb(struct sk_buff *skb, struct sc_chan *ch, struct sc_urb_data *urb_data)
{
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	struct sc_msg_can_tx *tx = urb_data->mem;
	unsigned data_len = 0;

	// netdev_dbg(ch->netdev, "tx can_id %x\n", CAN_EFF_MASK & cfd->can_id);

	tx->id = SC_MSG_CAN_TX;
	tx->track_id = urb_data - ch->tx_urb_ptr;
	tx->can_id = ch->usb_priv->host_to_dev32(CAN_EFF_MASK & cfd->can_id);
	tx->dlc = can_len2dlc(cfd->len);

	data_len = can_dlc2len(tx->dlc);
	// netdev_dbg(ch->netdev, "data len %u\n", data_len);

	tx->flags = 0;

	if (CAN_EFF_FLAG & cfd->can_id)
		tx->flags |= SC_CAN_FRAME_FLAG_EXT;

	if (cfd->can_id & CAN_RTR_FLAG) {
		urb_data->len = 0;
		tx->len = round_up(sizeof(*tx), 4);
		tx->flags |= SC_CAN_FRAME_FLAG_RTR;
	} else {
		urb_data->len = data_len;
		tx->len = round_up(sizeof(*tx) + data_len, 4);
		memcpy(tx->data, cfd->data, data_len);
	}

	if (can_is_canfd_skb(skb)) {
		tx->flags |= SC_CAN_FRAME_FLAG_FDF;
		if (cfd->flags & CANFD_BRS)
			tx->flags |= SC_CAN_FRAME_FLAG_BRS;
		if (cfd->flags & CANFD_ESI)
			tx->flags |= SC_CAN_FRAME_FLAG_ESI;
	}

	urb_data->urb->transfer_buffer_length = tx->len;
}

static netdev_tx_t sc_can_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct sc_net_priv *priv = netdev_priv(netdev);
	struct sc_chan *ch = priv->ch;
	struct sc_urb_data *urb_data = NULL;
	unsigned long flags = 0;
	unsigned urb_index = -1;
	netdev_tx_t rc = NETDEV_TX_OK;
	int error = 0;

	if (can_dropped_invalid_skb(netdev, skb))
		return NETDEV_TX_OK;

	spin_lock_irqsave(&ch->tx_lock, flags);
	if (ch->tx_urb_available_count) {
		urb_index = ch->tx_urb_available_ptr[--ch->tx_urb_available_count];
		BUG_ON(urb_index >= ch->tx_urb_count);
		urb_data = &ch->tx_urb_ptr[urb_index];
		BUG_ON(urb_data->flags);
		can_put_echo_skb(skb, netdev, urb_index);
	} else {
		netif_stop_queue(netdev);
		rc = NETDEV_TX_BUSY;
	}
	spin_unlock_irqrestore(&ch->tx_lock, flags);

	if (NETDEV_TX_OK != rc)
		return rc;


	sc_can_fill_tx_urb(skb, ch, urb_data);

	error = usb_submit_urb(urb_data->urb, GFP_ATOMIC);

	if (error) {
		spin_lock_irqsave(&ch->tx_lock, flags);
		// // remove echo
		can_free_echo_skb(netdev, urb_index);
		// put tx urb back
		ch->tx_urb_available_ptr[ch->tx_urb_available_count++] = urb_index;
		spin_unlock_irqrestore(&ch->tx_lock, flags);

		if (-ENODEV == error)
			netif_device_detach(netdev);
		else
			netdev_warn(netdev, "URB submit failed: %d\n", error);
	} else {
		// netdev_dbg(netdev, "submitted tx URB index %u\n", (unsigned)urb_index);
	}

	return rc;
}

static int sc_can_set_bittiming(struct net_device *netdev)
{
	netdev_dbg(netdev, "set nominal bittiming\n");
	return 0;
}

static int sc_can_set_data_bittiming(struct net_device *netdev)
{
	netdev_dbg(netdev, "set data bittiming\n");
	return 0;
}

static int sc_can_set_mode(struct net_device *netdev, enum can_mode mode)
{
	netdev_dbg(netdev, "set mode %d\n", mode);
	return 0;
}

static int sc_get_berr_counter(const struct net_device *netdev, struct can_berr_counter *bec)
{
	struct sc_net_priv *priv = netdev_priv(netdev);
	struct sc_chan *ch = priv->ch;
	*bec = ch->bec;
	return 0;
}

static const struct net_device_ops sc_can_netdev_ops = {
	.ndo_open = &sc_can_open,
	.ndo_stop = &sc_can_close,
	.ndo_start_xmit = &sc_can_start_xmit,
	.ndo_change_mtu = &can_change_mtu,
};


static void sc_can_chan_cleanup_urbs(struct sc_chan *ch)
{
	struct usb_device *udev = 0;
	unsigned i = 0;

	BUG_ON(!ch);
	BUG_ON(!ch->usb_priv);
	BUG_ON(!ch->usb_priv->intf);

	udev = interface_to_usbdev(ch->usb_priv->intf);

	if (ch->rx_urb_ptr) {
		for (i = 0; i < ch->rx_urb_count; ++i) {
			struct sc_urb_data *urb_data = &ch->rx_urb_ptr[i];
			usb_free_coherent(udev, ch->usb_priv->msg_buffer_size, urb_data->mem, urb_data->dma_addr);
			usb_free_urb(urb_data->urb);
		}
		ch->rx_urb_count = 0;
		kfree(ch->rx_urb_ptr);
		ch->rx_urb_ptr = NULL;
	}

	if (ch->tx_urb_ptr) {
		for (i = 0; i < ch->tx_urb_count; ++i) {
			struct sc_urb_data *urb_data = &ch->tx_urb_ptr[i];
			usb_free_coherent(udev, ch->usb_priv->msg_buffer_size, urb_data->mem, urb_data->dma_addr);
			usb_free_urb(urb_data->urb);
		}
		ch->tx_urb_count = 0;
		kfree(ch->tx_urb_ptr);
		ch->tx_urb_ptr = NULL;
	}

	if (ch->tx_urb_available_ptr) {
		kfree(ch->tx_urb_available_ptr);
		ch->tx_urb_available_ptr = NULL;
		ch->tx_urb_available_count = 0;
	}
}

static void sc_can_chan_uninit(struct sc_chan *ch)
{
	if (ch->netdev) {
		if (ch->registered)
			unregister_candev(ch->netdev);

		free_candev(ch->netdev);
		ch->netdev = NULL;
	}

	if (ch->tx_cmd_buffer) {
		kfree(ch->tx_cmd_buffer);
		ch->tx_cmd_buffer = NULL;
		ch->rx_cmd_buffer = NULL;
	}

	sc_can_chan_cleanup_urbs(ch);
}


static int sc_can_chan_alloc_urbs(struct sc_chan *ch)
{
	struct usb_device *udev = NULL;
	struct device *dev = NULL;
	unsigned rx_pipe = 0, rx_urbs = 0, tx_pipe = 0, tx_urbs = 0;
	size_t bytes = 0;

	BUG_ON(!ch);
	BUG_ON(!ch->usb_priv);
	BUG_ON(!ch->usb_priv->intf);

	udev = interface_to_usbdev(ch->usb_priv->intf);
	BUG_ON(!udev);

	dev = &ch->usb_priv->intf->dev;

	rx_pipe = usb_rcvbulkpipe(udev, ch->msg_epp);
	tx_pipe = usb_sndbulkpipe(udev, ch->msg_epp);
	bytes = ch->usb_priv->msg_buffer_size;
	rx_urbs = ch->usb_priv->rx_fifo_size;
	tx_urbs = ch->usb_priv->tx_fifo_size;

	ch->rx_urb_count = 0;
	ch->tx_urb_count = 0;
	ch->rx_urb_ptr = NULL;
	ch->tx_urb_ptr = NULL;

	ch->rx_urb_ptr = kcalloc(rx_urbs, sizeof(*ch->rx_urb_ptr), GFP_KERNEL);
	if (!ch->rx_urb_ptr) {
		goto err_no_mem;
	}

	ch->tx_urb_ptr = kcalloc(tx_urbs, sizeof(*ch->tx_urb_ptr), GFP_KERNEL);
	if (!ch->tx_urb_ptr) {
		goto err_no_mem;
	}

	ch->tx_urb_available_ptr = kcalloc(tx_urbs, sizeof(*ch->tx_urb_available_ptr), GFP_KERNEL);
	if (!ch->tx_urb_available_ptr) {
		goto err_no_mem;
	}

	for (ch->rx_urb_count = 0; ch->rx_urb_count < rx_urbs; ++ch->rx_urb_count) {
		struct sc_urb_data *urb_data = &ch->rx_urb_ptr[ch->rx_urb_count];
		urb_data->ch = ch;
		urb_data->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb_data->urb) {
			dev_err(dev, "URB allocation failed\n");
			break;
		}

		urb_data->mem = usb_alloc_coherent(udev, bytes, GFP_KERNEL, &urb_data->dma_addr);
		if (!urb_data->mem) {
			dev_err(dev, "URB coherent mem allocation failed\n");
			usb_free_urb(urb_data->urb);
			urb_data->urb = NULL;
			break;
		}

		usb_fill_bulk_urb(urb_data->urb, udev, rx_pipe, urb_data->mem, bytes, &sc_usb_rx_completed, urb_data);
		urb_data->urb->transfer_dma = urb_data->dma_addr;
		urb_data->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	if (0 == ch->rx_urb_count) {
		dev_err(&ch->usb_priv->intf->dev, "no rx URBs\n");
		goto err_no_mem;
	}

	if (ch->rx_urb_count < rx_urbs)
		dev_warn(&ch->usb_priv->intf->dev, "only %u/%u rx URBs allocated\n", ch->rx_urb_count, rx_urbs);


	for (ch->tx_urb_count = 0; ch->tx_urb_count < tx_urbs; ++ch->tx_urb_count) {
		struct sc_urb_data *urb_data = &ch->tx_urb_ptr[ch->tx_urb_count];
		urb_data->ch = ch;
		urb_data->urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb_data->urb) {
			dev_err(dev, "URB allocation failed\n");
			break;
		}

		urb_data->mem = usb_alloc_coherent(udev, bytes, GFP_KERNEL, &urb_data->dma_addr);
		if (!urb_data->mem) {
			dev_err(dev, "URB coherent mem allocation failed\n");
			usb_free_urb(urb_data->urb);
			urb_data->urb = NULL;
			break;
		}

		usb_fill_bulk_urb(urb_data->urb, udev, tx_pipe, urb_data->mem, bytes, &sc_usb_tx_completed, urb_data);
		urb_data->urb->transfer_dma = urb_data->dma_addr;
		urb_data->urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		ch->tx_urb_available_ptr[ch->tx_urb_count] = ch->tx_urb_count;
	}

	if (0 == ch->tx_urb_count) {
		dev_err(dev, "no tx URBs\n");
		goto err_no_mem;
	}

	if (ch->tx_urb_count < tx_urbs)
		dev_warn(dev, "only %u/%u tx URBs allocated\n", ch->tx_urb_count, tx_urbs);

	ch->tx_urb_available_count = ch->tx_urb_count;

	return 0;

err_no_mem:
	sc_can_chan_cleanup_urbs(ch);

	return -ENOMEM;
}

static int sc_can_chan_init(struct sc_chan *ch)
{
	struct sc_net_priv *priv = NULL;
	int rc = 0;

	BUG_ON(!ch);
	BUG_ON(!ch->usb_priv);

	spin_lock_init(&ch->tx_lock);

	ch->tx_cmd_buffer = kmalloc(2 * ch->usb_priv->cmd_buffer_size, GFP_KERNEL);
	if (!ch->tx_cmd_buffer) {
		rc = -ENOMEM;
		goto fail;
	}

	/* rx cmd buffer is part of tx cmd buffer */
	ch->rx_cmd_buffer = ch->tx_cmd_buffer + ch->usb_priv->cmd_buffer_size;

	rc = sc_can_chan_alloc_urbs(ch);
	if (rc)
		goto fail;


	ch->netdev = alloc_candev(sizeof(*priv), ch->usb_priv->tx_fifo_size);
	if (!ch->netdev) {
		dev_err(&ch->usb_priv->intf->dev, "candev alloc failed\n");
		rc = -ENOMEM;
		goto fail;
	}

	ch->netdev->flags |= IFF_ECHO;
	ch->netdev->netdev_ops = &sc_can_netdev_ops;
	SET_NETDEV_DEV(ch->netdev, &ch->usb_priv->intf->dev);

	priv = netdev_priv(ch->netdev);
	priv->ch = ch;

	// Next line, if uncommented, effectively disables CAN-FD mode
	// priv->can.ctrlmode_static = ch->usb_priv->ctrlmode_static;
	priv->can.ctrlmode_supported = ch->usb_priv->ctrlmode_supported | ch->usb_priv->ctrlmode_static;
	priv->can.state = CAN_STATE_STOPPED;
	priv->can.clock.freq = ch->usb_priv->can_clock_hz;

	priv->can.do_set_mode = &sc_can_set_mode;
	priv->can.do_get_berr_counter = &sc_get_berr_counter;
	priv->can.bittiming_const = &ch->usb_priv->nominal;
	priv->can.do_set_bittiming = &sc_can_set_bittiming;

	if (ch->usb_priv->ctrlmode_supported & CAN_CTRLMODE_FD) {
		priv->can.data_bittiming_const = &ch->usb_priv->data;
		priv->can.do_set_data_bittiming = &sc_can_set_data_bittiming;
	}


	rc = register_candev(ch->netdev);
	if (rc) {
		dev_err(&ch->usb_priv->intf->dev, "candev registration failed\n");
		goto fail;
	}

	ch->registered = 1;
	netdev_dbg(ch->netdev, "candev registered\n");

out:
	return rc;

fail:
	sc_can_chan_uninit(ch);
	goto out;
}


static void sc_usb_cleanup(struct sc_usb_priv *priv)
{
	unsigned i = 0;

	if (priv) {
		if (priv->chan_ptr) {
			for (i = 0; i < priv->chan_count; ++i)
				sc_can_chan_uninit(&priv->chan_ptr[i]);

			kfree(priv->chan_ptr);
		}

		kfree(priv);
	}
}

static void sc_usb_disconnect(struct usb_interface *intf)
{
	struct sc_usb_priv *priv = usb_get_intfdata(intf);

	usb_set_intfdata(intf, NULL);

	if (priv)
		sc_usb_cleanup(priv);
}

static int sc_usb_probe_prelim(struct usb_interface *intf, u8 *cmd_epp, u16 *cmd_ep_size, u8 *msg_epp)
{
	struct usb_host_endpoint *ep = NULL;

	// ensure interface is set
	if (unlikely(!intf->cur_altsetting)) {
		return -ENODEV;
	}

	// we want a VENDOR interface
	if (intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC) {
		dev_dbg(&intf->dev, "not a vendor interface: %#02x\n", intf->cur_altsetting->desc.bInterfaceClass);
		return -ENODEV;
	}

	// must have at least two endpoint pairs
	dev_dbg(&intf->dev, "device has %u eps\n", intf->cur_altsetting->desc.bNumEndpoints);
	if (unlikely(intf->cur_altsetting->desc.bNumEndpoints / 2 < 2)) {
		return -ENODEV;
	}

	ep = intf->cur_altsetting->endpoint;

	// endpoint must be bulk
	if (unlikely(usb_endpoint_type(&ep->desc) != USB_ENDPOINT_XFER_BULK)) {
		return -ENODEV;
	}

	*cmd_ep_size = le16_to_cpu(ep->desc.wMaxPacketSize);
	dev_dbg(&intf->dev, "cmd ep size %u\n", *cmd_ep_size);

	// endpoint too small?
	if (unlikely(*cmd_ep_size < SC_MIN_SUPPORTED_TRANSFER_SIZE)) {
		return -ENODEV;
	}

	*cmd_epp = usb_endpoint_num(&ep->desc);
	dev_dbg(&intf->dev, "cmd ep num %u\n", *cmd_epp);

	*msg_epp = usb_endpoint_num(&(ep + 2)->desc);
	dev_dbg(&intf->dev, "msg ep num %u\n", *msg_epp);

	return 0;
}


static int sc_usb_probe_dev(struct sc_usb_priv *priv, u16 ep_size)
{
	struct sc_msg_req *req;
	struct sc_msg_hello *device_hello;
	struct sc_msg_dev_info *device_info;
	struct sc_msg_can_info *can_info;
	struct usb_device *udev;
	void *tx_buf = NULL;
	void *rx_buf = NULL;
	char serial_str[1 + sizeof(device_info->sn_bytes)*2];
	char name_str[1 + sizeof(device_info->name_bytes)];
	unsigned i = 0;
	int rc = 0;
	int actual_len = 0;
	int msg_buffer_size_supports_canfd = 0;


	BUG_ON(!priv);
	BUG_ON(!priv->intf);
	BUG_ON(ep_size < SC_MIN_SUPPORTED_TRANSFER_SIZE);

	udev = interface_to_usbdev(priv->intf);

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


	dev_dbg(&priv->intf->dev, "sending SC_MSG_HELLO_DEVICE on %02x\n", priv->cmd_epp);
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, priv->cmd_epp), req,
			sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	/* wait for response */
	dev_dbg(&priv->intf->dev, "waiting for SC_MSG_HELLO_HOST\n");
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, priv->cmd_epp), rx_buf, ep_size, &actual_len, SC_USB_TIMEOUT_MS);
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

	priv->cmd_buffer_size = ntohs(device_hello->cmd_buffer_size);

	if (0 == device_hello->proto_version
		|| priv->cmd_buffer_size < SC_MIN_SUPPORTED_TRANSFER_SIZE) {
		dev_err(&priv->intf->dev,
			"badly configured device: proto_version=%u cmd_buffer_size=%u\n",
			device_hello->proto_version, priv->cmd_buffer_size);
		rc = -ENODEV;
		goto err;
	}

	if (device_hello->proto_version > SC_VERSION) {
		dev_info(&priv->intf->dev, "device proto version %u exceeds supported version %u\n",
			device_hello->proto_version, SC_VERSION);
		rc = -ENODEV;
		goto err;
	}

	/* At this point we are fairly confident we are dealing with the genuine article. */
	dev_info(
		&priv->intf->dev,
		"device proto version %u, %s endian\n",
		device_hello->proto_version, device_hello->byte_order == SC_BYTE_ORDER_LE ? "little" : "big");

	if (SC_NATIVE_BYTE_ORDER == device_hello->byte_order) {
		priv->host_to_dev16 = &sc_nop16;
		priv->host_to_dev32 = &sc_nop32;
	} else {
		priv->host_to_dev16 = &sc_swap16;
		priv->host_to_dev32 = &sc_swap32;
	}

	// realloc cmd rx buffer if device reports larger cap
	if (ep_size < priv->cmd_buffer_size)
		if (ksize(rx_buf) < priv->cmd_buffer_size) {
			kfree(rx_buf);
			rx_buf = kmalloc(priv->cmd_buffer_size, GFP_KERNEL);
			if (!rx_buf) {
				rc = -ENOMEM;
				goto err;
			}
		}

	req->id = SC_MSG_DEVICE_INFO;
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, priv->cmd_epp), req, sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	/* wait for response */
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, priv->cmd_epp), rx_buf, priv->cmd_buffer_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	device_info = rx_buf;

	if (unlikely(actual_len < 0 ||
		(size_t)actual_len < sizeof(*device_info) ||
		SC_MSG_DEVICE_INFO != device_info->id ||
		device_info->len < sizeof(*device_info))) {
		dev_err(&priv->intf->dev, "bad reply to SC_MSG_DEVICE_INFO (%d bytes)\n", actual_len);
		rc = -ENODEV;
		goto err;
	}

	priv->feat_perm = priv->host_to_dev16(device_info->feat_perm);
	priv->feat_conf = priv->host_to_dev16(device_info->feat_conf);
	dev_info(&priv->intf->dev, "device features perm=%04x conf=%04x\n",
		priv->feat_perm, priv->feat_conf);


	for (i = 0; i < min((size_t)device_info->sn_len, ARRAY_SIZE(serial_str)-1); ++i)
		snprintf(&serial_str[i*2], 3, "%02x", device_info->sn_bytes[i]);

	serial_str[i*2] = 0;

	device_info->name_len = min((size_t)device_info->name_len, sizeof(name_str)-1);
	memcpy(name_str, device_info->name_bytes, device_info->name_len);
	name_str[device_info->name_len] = 0;

	dev_info(&priv->intf->dev, "device %s, serial %s, firmware version %u.%u.%u\n",
		name_str, serial_str, device_info->fw_ver_major, device_info->fw_ver_minor, device_info->fw_ver_patch);


	req->id = SC_MSG_CAN_INFO;
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, priv->cmd_epp), req, sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	/* wait for response */
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, priv->cmd_epp), rx_buf, priv->cmd_buffer_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto err;

	can_info = rx_buf;

	if (unlikely(actual_len < 0 ||
				 (size_t)actual_len < sizeof(*can_info) ||
				 SC_MSG_CAN_INFO != can_info->id ||
				 can_info->len < sizeof(*can_info))) {
		dev_err(&priv->intf->dev, "bad reply to SC_MSG_CAN_INFO (%d bytes)\n", actual_len);
		rc = -ENODEV;
		goto err;
	}

	priv->chan_count = 1;
	priv->can_clock_hz = priv->host_to_dev32(can_info->can_clk_hz);
	priv->tx_fifo_size = min(can_info->tx_fifo_size, (u8)SC_MAX_TX_URBS);
	priv->rx_fifo_size = min(can_info->rx_fifo_size, (u8)SC_MAX_RX_URBS);
	priv->msg_buffer_size = priv->host_to_dev16(can_info->msg_buffer_size);

	if (priv->msg_buffer_size < round_up(
			CAN_MAX_DLEN + max(sizeof(struct sc_msg_can_rx), sizeof(struct sc_msg_can_tx)), 4)) {
		rc = -ENODEV;
		goto err;
	}

	msg_buffer_size_supports_canfd =
		priv->msg_buffer_size >= round_up(CANFD_MAX_DLEN
			+ max(sizeof(struct sc_msg_can_rx), sizeof(struct sc_msg_can_tx)), 4);


	// nominal bitrate
	strlcpy(priv->nominal.name, SC_NAME, sizeof(priv->nominal.name));
	priv->nominal.tseg1_min = can_info->nmbt_tseg1_min;
	priv->nominal.tseg1_max = priv->host_to_dev16(can_info->nmbt_tseg1_max);
	priv->nominal.tseg2_min = can_info->nmbt_tseg2_min;
	priv->nominal.tseg2_max = can_info->nmbt_tseg2_max;
	priv->nominal.sjw_max = can_info->nmbt_sjw_max;
	priv->nominal.brp_min = can_info->nmbt_brp_min;
	priv->nominal.brp_max = priv->host_to_dev16(can_info->nmbt_brp_max);
	priv->nominal.brp_inc = 1;

	// data bitrate
	strlcpy(priv->data.name, SC_NAME, sizeof(priv->data.name));
	priv->data.tseg1_min = can_info->dtbt_tseg1_min;
	priv->data.tseg1_max = can_info->dtbt_tseg1_max;
	priv->data.tseg2_min = can_info->dtbt_tseg2_min;
	priv->data.tseg2_max = can_info->dtbt_tseg2_max;
	priv->data.sjw_max = can_info->dtbt_sjw_max;
	priv->data.brp_min = can_info->dtbt_brp_min;
	priv->data.brp_max = can_info->dtbt_brp_max;
	priv->data.brp_inc = 1;

	// FIX ME: add code paths for permanently enabled features
	priv->ctrlmode_static |= CAN_CTRLMODE_BERR_REPORTING;

	if (priv->feat_conf & SC_FEATURE_FLAG_FDF) {
		dev_info(&priv->intf->dev, "device supports CAN-FD\n");
		if (msg_buffer_size_supports_canfd)
			priv->ctrlmode_supported |= CAN_CTRLMODE_FD;
		else
			dev_warn(&priv->intf->dev, "CAN-FD disabled, device message buffer too small (%u)\n", priv->msg_buffer_size);
	}

	if (priv->feat_conf & SC_FEATURE_FLAG_MON_MODE) {
		dev_info(&priv->intf->dev, "device supports monitoring mode\n");
		priv->ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;
	}

	if (priv->feat_conf & SC_FEATURE_FLAG_EXT_LOOP_MODE) {
		dev_info(&priv->intf->dev, "device supports external loopback mode\n");
		priv->ctrlmode_supported |= CAN_CTRLMODE_LOOPBACK;
	}

	if (priv->feat_conf & SC_FEATURE_FLAG_DAR) {
		dev_info(&priv->intf->dev, "device supports one-shot mode\n");
		priv->ctrlmode_supported |= CAN_CTRLMODE_ONE_SHOT;
	}

	if (!((priv->feat_perm | priv->feat_conf) & SC_FEATURE_FLAG_TXR)) {
		dev_err(&priv->intf->dev, "device doesn't support txr feature required by this driver\n");
		rc = -ENODEV;
		goto err;
	}

	priv->chan_ptr = kcalloc(priv->chan_count, sizeof(*priv->chan_ptr), GFP_KERNEL);
	if (!priv->chan_ptr) {
		rc = -ENOMEM;
		goto err;
	}

	for (i = 0; i < priv->chan_count; ++i) {
		priv->chan_ptr[i].usb_priv = priv;
		priv->chan_ptr[i].index = i;
		priv->chan_ptr[i].cmd_epp = priv->cmd_epp;
		priv->chan_ptr[i].msg_epp = priv->msg_epp;
		rc = sc_can_chan_init(&priv->chan_ptr[i]);
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
	u8 cmd_epp = 0;
	u8 msg_epp = 0;
	u16 cmd_ep_size = 0;
	struct sc_usb_priv *priv = NULL;

	(void)id; // unused

	rc = sc_usb_probe_prelim(intf, &cmd_epp, &cmd_ep_size, &msg_epp);
	if (rc)
		goto err;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		rc = -ENOMEM;
		goto err;
	}

	priv->intf = intf;
	priv->cmd_epp = cmd_epp;
	priv->msg_epp = msg_epp;

	rc = sc_usb_probe_dev(priv, cmd_ep_size);
	if (rc)
		goto err;

	usb_set_intfdata(intf, priv);

out:
	return rc;

err:
	sc_usb_cleanup(priv);
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