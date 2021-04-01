// SPDX-License-Identifier: (GPL-2.0 or MIT)

/*
 * Copyright (c) 2020-2021 Jean Gressmann <jean@0x42.de>
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
#define SC_FIFO_CHANGE_LOG_THRESHOLD 8
#define SC_CLOCK_BITS 32
#define SC_CLOCK_MAX ((u32)((((u64)1) << SC_CLOCK_BITS)-1))

#define SC_VERIFY(condition, recovery) \
	do { \
		if (unlikely(!(condition))) { \
			static const char expr[] = "" #condition ""; \
			pr_crit(SC_NAME " check failed: '%s' @ %s:%d\n", expr, __func__, __LINE__); \
			do { recovery; } while (0); \
		} \
	} while (0)

#ifndef DEBUG
#define DEBUG 0
#endif

#if DEBUG
	#define SC_ASSERT(x) \
		do { \
			static const char expr[] = "" #x ""; \
			if (unlikely(!(x))) { \
				pr_crit(SC_NAME " assertion failed: '%s' @ %s:%d\n", expr, __func__, __LINE__); \
				BUG(); \
			} \
		} while (0)

	#define SC_DEBUG_VERIFY SC_VERIFY
#else
	#define SC_ASSERT(x)
	#define SC_DEBUG_VERIFY(x, y)
#endif

#include "sc.h"


struct sc_usb_priv;
struct sc_net_priv {
	struct can_priv can; /* must be the first member */
	struct sc_usb_priv *usb;
	struct can_berr_counter bec;
};

struct sc_urb_data {
	struct sc_usb_priv *usb_priv;
	struct urb *urb;
	void *mem;
	dma_addr_t dma_addr;
};

/* usb interface struct */
struct sc_dev_time_tracker {
	uint32_t ts_us_lo;
	uint32_t ts_us_hi;
	uint32_t ts_initialized;
};

/* usb interface struct */
struct sc_usb_priv {
	struct usb_interface *intf;
	u16 (*host_to_dev16)(u16 value);
	u32 (*host_to_dev32)(u32 value);
	struct net_device *netdev;
	struct sc_urb_data *rx_urb_ptr;
	struct sc_urb_data *tx_urb_ptr;
	u8 *tx_cmd_buffer;
	u8 *rx_cmd_buffer;              /* points into tx_cmd_buffer mem, don't free */
	u8 *tx_urb_available_ptr;
	u8 *tx_echo_skb_available_ptr;
	struct can_bittiming_const nominal;
	struct can_bittiming_const data;
	struct sc_dev_time_tracker device_time_tracker;
	spinlock_t tx_lock;
	//spinlock_t rx_lock;
	u32 can_clock_hz;
	u32 ctrlmode_static;
	u32 ctrlmode_supported;
	u16 feat_perm;
	u16 feat_conf;
	u16 cmd_buffer_size;
	u16 msg_buffer_size;
	u16 ep_size;
	u8 cmd_epp;
	u8 msg_epp;
	u8 static_tx_fifo_size;
	u8 static_rx_fifo_size;
	u8 registered;
	u8 rx_urb_count;
	u8 tx_urb_count;
	u8 tx_urb_available_count;
	u8 tx_echo_skb_available_count;
	u8 prev_rx_fifo_size;
	u8 prev_tx_fifo_size;
	u8 opened;
#ifdef DEBUG
	atomic_t rx_enter;
#endif
};


static u16 sc_usb_nop16(u16 value)
{
	return value;
}

static u32 sc_usb_nop32(u32 value)
{
	return value;
}

static u16 sc_usb_swap16(u16 value)
{
	return __builtin_bswap16(value);
}

static u32 sc_usb_swap32(u32 value)
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

static inline u16 sc_chunk_byte_swap(void *ctx, u16 value)
{
	return ((struct sc_usb_priv *)ctx)->host_to_dev16(value);
}


static inline void sc_usb_ktime_from_us(
	struct sc_usb_priv *usb_priv, u32 timestamp_us, ktime_t *duration)
{
	struct sc_dev_time_tracker *t = NULL;
	u64 ts_us = 0;

	SC_ASSERT(usb_priv);

	t = &usb_priv->device_time_tracker;
	timestamp_us &= SC_CLOCK_MAX;

	if (likely(t->ts_initialized)) {
		uint32_t delta = (timestamp_us - t->ts_us_lo) & SC_CLOCK_MAX;

		if (likely(delta < (SC_CLOCK_MAX / 4) * 3)) { // forward and plausible
			if (unlikely(timestamp_us < t->ts_us_lo)) {
				++t->ts_us_hi;
				netdev_dbg(usb_priv->netdev, "inc ts high=%lu\n", (unsigned long)t->ts_us_hi);
			}

			t->ts_us_lo = timestamp_us;
			ts_us = ((u64)t->ts_us_hi << SC_CLOCK_BITS) | timestamp_us;
		} else {
			// netdev_dbg(usb_priv->netdev, "late ts=%lu lo=%lu\n", (unsigned long)timestamp_us, (unsigned long)t->ts_us_lo);
			ts_us = ((u64)t->ts_us_hi << SC_CLOCK_BITS) | t->ts_us_lo;
			ts_us -= (t->ts_us_lo - timestamp_us) & SC_CLOCK_MAX;
		}
	} else {
		netdev_dbg(usb_priv->netdev, "init ts=%lu\n", (unsigned long)timestamp_us);
		t->ts_initialized = 1;
		t->ts_us_lo = timestamp_us;
		ts_us = timestamp_us;
	}

	*duration = ns_to_ktime(ts_us * 1000);
}

static inline void sc_usb_update_ktime_from_us(struct sc_usb_priv *usb_priv, u32 timestamp_us)
{
	ktime_t unused;

	sc_usb_ktime_from_us(usb_priv, timestamp_us, &unused);
}

static int sc_usb_cmd_send_receive(
	struct sc_usb_priv *usb_priv, u8 ep, void *tx_ptr, int tx_len, void *rx_ptr)
{
	struct usb_device *udev = interface_to_usbdev(usb_priv->intf);
	struct sc_msg_error *error_msg = rx_ptr;
	int rc = 0;
	int tx_actual_len = 0, rx_actual_len = 0;

	/* send */
	dev_dbg(&usb_priv->intf->dev, "send %d bytes on ep %u\n", tx_len, ep);
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, ep), tx_ptr, tx_len, &tx_actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		return rc;

	if (unlikely(tx_actual_len != tx_len))
		return -EPIPE;

	/* wait for response */
	dev_dbg(&usb_priv->intf->dev, "wait for data on ep %u\n", ep);
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, ep), rx_ptr, usb_priv->cmd_buffer_size, &rx_actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		return rc;

	/* check for short reply */
	if (unlikely(
		rx_actual_len < 0 ||
		(size_t)rx_actual_len < sizeof(*error_msg))) {

		dev_err(&usb_priv->intf->dev, "short reply\n");
		return -ETOOSMALL;
	}

	/* check for proper message id */
	if (unlikely(error_msg->id != SC_MSG_ERROR)) {
		dev_err(&usb_priv->intf->dev,
			"unexpected response id %#02x expected %#02x (SC_MSG_ERROR)\n",
			error_msg->id, SC_MSG_ERROR);
		return -EPROTO;
	}

	dev_dbg(&usb_priv->intf->dev, "response received, error code %d\n", error_msg->error);

	return sc_usb_map_error(error_msg->error);
}

static inline int sc_cmd_send_receive(struct sc_usb_priv *usb_priv, unsigned int cmd_len)
{
	SC_ASSERT(usb_priv);
	SC_ASSERT(cmd_len);
	SC_ASSERT(cmd_len <= usb_priv->cmd_buffer_size);
	return sc_usb_cmd_send_receive(usb_priv, usb_priv->cmd_epp, usb_priv->tx_cmd_buffer, cmd_len, usb_priv->rx_cmd_buffer);
}


static int sc_usb_cmd_bus(struct sc_usb_priv *usb_priv, int on)
{
	struct sc_msg_config *bus = (struct sc_msg_config *)usb_priv->tx_cmd_buffer;
	int rc = 0;

	memset(bus, 0, sizeof(*bus));
	bus->id = SC_MSG_BUS;
	bus->len = sizeof(*bus);
	bus->arg = usb_priv->host_to_dev16(on);

	netdev_dbg(usb_priv->netdev, "bus %s\n", on ? "on" : "off");
	rc = sc_cmd_send_receive(usb_priv, sizeof(*bus));
	if (rc)
		return rc;

	return 0;
}

static int sc_usb_netdev_close(struct net_device *netdev)
{
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct sc_usb_priv *usb_priv = net_priv->usb;
	int rc = 0;
	unsigned int i = 0;
	unsigned long flags = 0;

	netdev_dbg(netdev, "stop queue\n");
	netif_stop_queue(netdev);



	//spin_lock_irqsave(&usb_priv->rx_lock, flags);
	//usb_priv->opened = 0;
	//spin_unlock_irqrestore(&usb_priv->rx_lock, flags);
	WRITE_ONCE(usb_priv->opened, 0);


	rc = sc_usb_cmd_bus(usb_priv, 0);
	if (rc)
		netdev_dbg(netdev, "bus off failed (%d)\n", rc);


	for (i = 0; i < usb_priv->rx_urb_count; ++i) {
		struct sc_urb_data *urb_data = &usb_priv->rx_urb_ptr[i];

		usb_kill_urb(urb_data->urb);
	}

	for (i = 0; i < usb_priv->tx_urb_count; ++i) {
		struct sc_urb_data *urb_data = &usb_priv->tx_urb_ptr[i];

		usb_kill_urb(urb_data->urb);
	}

	spin_lock_irqsave(&usb_priv->tx_lock, flags);

	// reset tx urb flags, fill available list
	for (i = 0; i < usb_priv->tx_urb_count; ++i)
		usb_priv->tx_urb_available_ptr[i] = i;

	usb_priv->tx_urb_available_count = usb_priv->tx_urb_count;

	for (i = 0; i < net_priv->can.echo_skb_max; ++i) {
		can_free_echo_skb(netdev, i);
		usb_priv->tx_echo_skb_available_ptr[i] = i;
	}

	usb_priv->tx_echo_skb_available_count = net_priv->can.echo_skb_max;

	spin_unlock_irqrestore(&usb_priv->tx_lock, flags);


	// spin_lock_irqsave(&usb_priv->rx_lock, flags);

	// reset time
	memset(&usb_priv->device_time_tracker, 0, sizeof(usb_priv->device_time_tracker));


	usb_priv->prev_rx_fifo_size = 0;
	usb_priv->prev_tx_fifo_size = 0;

	// spin_unlock_irqrestore(&usb_priv->rx_lock, flags);

	netdev_dbg(netdev, "close candev\n");
	(void)close_candev(netdev);

	net_priv->can.state = CAN_STATE_STOPPED; // mark as down else last CAN state

	return 0;
}


static int sc_usb_process_can_error(struct sc_usb_priv *usb_priv, struct sc_msg_can_error *error)
{
	struct net_device *netdev = usb_priv->netdev;
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct net_device_stats *net_stats = &netdev->stats;
	struct can_device_stats *can_stats = &net_priv->can.can_stats;
	struct sk_buff *skb = NULL;
	struct can_frame *cf = NULL;

	if (unlikely(error->len < sizeof(*error))) {
		netdev_err(netdev, "short sc_msg_can_error (%u)\n", error->len);
		return -ETOOSMALL;
	}

	if (unlikely(error->error == SC_CAN_ERROR_NONE))
		return 0;


	skb = alloc_can_err_skb(netdev, &cf);
	if (unlikely(!skb))
		return -ENOMEM;


	if (error->error == SC_CAN_ERROR_ACK)
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

static int sc_usb_process_can_status(struct sc_usb_priv *usb_priv, struct sc_msg_can_status *status)
{
	struct net_device *netdev = usb_priv->netdev;
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct net_device_stats *net_stats = &netdev->stats;
	struct sk_buff *skb = NULL;
	struct can_frame *cf = NULL;
	struct can_frame f = {
		.can_id = CAN_ERR_FLAG,
		.can_dlc = CAN_ERR_DLC,
		.data = {0, 0, 0, 0, 0, 0, 0, 0},
	};
	enum can_state curr_state = 0, next_state = 0;
	u16 rx_lost = 0;
	u16 tx_dropped = 0;

	if (unlikely(status->len < sizeof(*status))) {
		netdev_err(netdev, "short sc_msg_can_status (%u)\n", status->len);
		return -ETOOSMALL;
	}

	if (status->rx_fifo_size >= usb_priv->static_rx_fifo_size / 2 &&
		status->rx_fifo_size != usb_priv->prev_rx_fifo_size) {
		if (net_ratelimit())
			netdev_dbg(netdev, "rx fs=%u\n", status->rx_fifo_size);

		usb_priv->prev_rx_fifo_size = status->rx_fifo_size;
	}

	if (status->tx_fifo_size >= usb_priv->static_tx_fifo_size / 2 &&
		status->tx_fifo_size != usb_priv->prev_tx_fifo_size) {
		if (net_ratelimit())
			netdev_dbg(netdev, "tx fs=%u\n", status->tx_fifo_size);

		usb_priv->prev_tx_fifo_size = status->tx_fifo_size;
	}
	//if (status->tx_fifo_size != usb_priv->prev_tx_fifo_size) {
	//if (net_ratelimit())
	//		netdev_dbg(netdev, "tx fs=%u\n", status->tx_fifo_size);

	//	usb_priv->prev_tx_fifo_size = status->tx_fifo_size;
	//}

	sc_usb_update_ktime_from_us(usb_priv, usb_priv->host_to_dev32(status->timestamp_us));

	rx_lost = usb_priv->host_to_dev16(status->rx_lost);
	tx_dropped = usb_priv->host_to_dev16(status->tx_dropped);

	if (unlikely(rx_lost)) {
		if (net_ratelimit())
			netdev_dbg(netdev, "rx lost %u frames in device\n", rx_lost);
		net_stats->rx_over_errors += rx_lost;
		f.can_id |= CAN_ERR_CRTL;
		f.data[1] |= CAN_ERR_CRTL_RX_OVERFLOW;
	}

	if (unlikely(tx_dropped)) {
		if (net_ratelimit())
			netdev_dbg(netdev, "tx dropped %u frames in device\n", tx_dropped);
		net_stats->tx_dropped += tx_dropped;
		f.can_id |= CAN_ERR_CRTL;
		f.data[1] |= CAN_ERR_CRTL_TX_OVERFLOW;
	}

	f.data[5] = status->rx_errors;
	f.data[6] = status->tx_errors;

	net_priv->bec.rxerr = status->rx_errors;
	net_priv->bec.txerr = status->tx_errors;


	curr_state = net_priv->can.state;

	if (status->flags & SC_CAN_STATUS_FLAG_TXR_DESYNC) {
		/* Go bus off. Restarting the bus will clear desync. */
		next_state = CAN_STATE_BUS_OFF;
		if (net_ratelimit())
			netdev_err(netdev, "txr desync\n");
	} else if (status->bus_status == SC_CAN_STATUS_BUS_OFF) {
		next_state = CAN_STATE_BUS_OFF;
	} else if (status->bus_status == SC_CAN_STATUS_ERROR_PASSIVE) {
		next_state = CAN_STATE_ERROR_PASSIVE;
	} else if (status->bus_status == SC_CAN_STATUS_ERROR_WARNING) {
		next_state = CAN_STATE_ERROR_WARNING;
	} else
		next_state = CAN_STATE_ERROR_ACTIVE;

	if (next_state != curr_state) {
		netdev_info(netdev, "can bus status 0x%02x -> 0x%02x\n", curr_state, next_state);
		can_change_state(netdev, &f, next_state, next_state);
		if (next_state == CAN_STATE_BUS_OFF)
			can_bus_off(netdev);
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

static int sc_usb_process_can_rx(struct sc_usb_priv *usb_priv, struct sc_msg_can_rx *rx)
{
	struct net_device *netdev = usb_priv->netdev;
	struct net_device_stats *net_stats = &netdev->stats;
	struct sk_buff *skb = NULL;
	struct canfd_frame *cf = NULL;
	unsigned int data_len = 0;
	canid_t can_id = 0;
	u32 timestamp_us = 0;

	if (unlikely(rx->len < sizeof(*rx))) {
		dev_err(
			&usb_priv->intf->dev,
			"short sc_msg_can_rx (%u)\n",
			rx->len);
		return -ETOOSMALL;
	}

	data_len = can_dlc2len(rx->dlc);
	can_id = usb_priv->host_to_dev32(rx->can_id);

	if (rx->flags & SC_CAN_FRAME_FLAG_RTR) {
		can_id |= CAN_RTR_FLAG;
	} else {
		if (unlikely(rx->len < sizeof(*rx) + data_len)) {
			dev_err(
				&usb_priv->intf->dev,
				"short sc_msg_can_rx (%u)\n",
				rx->len);
			return -ETOOSMALL;
		}
	}

	timestamp_us = usb_priv->host_to_dev32(rx->timestamp_us);


	if (rx->flags & SC_CAN_FRAME_FLAG_EXT)
		can_id |= CAN_EFF_FLAG;

	if (rx->flags & SC_CAN_FRAME_FLAG_FDF) {
		skb = alloc_canfd_skb(netdev, &cf);
		if (!skb) {
			++net_stats->rx_dropped;
			netdev_dbg(netdev, "rx dropped\n");
			return 0;
		}

		if (rx->flags & SC_CAN_FRAME_FLAG_BRS)
			cf->flags |= CANFD_BRS;

		if (rx->flags & SC_CAN_FRAME_FLAG_ESI)
			cf->flags |= CANFD_ESI;

		memcpy(cf->data, rx->data, data_len);
		netdev->stats.rx_bytes += data_len;
	} else {
		skb = alloc_can_skb(netdev, (struct can_frame **)&cf);
		if (!skb) {
			++net_stats->rx_dropped;
			netdev_dbg(netdev, "rx dropped\n");
			return 0;
		}

		if (!(rx->flags & SC_CAN_FRAME_FLAG_RTR) && data_len) {
			memcpy(cf->data, rx->data, data_len);
			netdev->stats.rx_bytes += data_len;
		}
	}

	cf->can_id = can_id;
	cf->len = data_len;


	++netdev->stats.rx_packets;

	sc_usb_ktime_from_us(usb_priv, timestamp_us, &skb_hwtstamps(skb)->hwtstamp);


	// netdev_dbg(netdev, "ts=%lu delta %ld (hex) %lx\n", (unsigned long)timestamp_us, (long)(timestamp_us - usb_priv->rx_last), (long)(timestamp_us - usb_priv->rx_last));
	// usb_priv->rx_last = timestamp_us;

	netif_rx(skb);

	return 0;
}

static int sc_usb_process_can_txr(struct sc_usb_priv *usb_priv, struct sc_msg_can_txr *txr)
{
	struct net_device *netdev = usb_priv->netdev;
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	ktime_t hwts;
	unsigned long flags = 0;
	u32 timestamp_us = 0;
	u8 echo_skb_index = 0;

	SC_ASSERT(usb_priv);
	SC_ASSERT(txr);

	if (unlikely(txr->len < sizeof(*txr))) {
		netdev_warn(netdev, "short sc_msg_can_txr (%u)\n", txr->len);
		return -ETOOSMALL;
	}

	echo_skb_index = txr->track_id;

	if (unlikely(echo_skb_index >= net_priv->can.echo_skb_max)) {
		netdev_warn(netdev, "out of bounds txr id %u (max %u)\n", echo_skb_index, net_priv->can.echo_skb_max);
		return -ERANGE;
	}

	//netdev_dbg(netdev, "txr echo %u %s\n", echo_skb_index, (txr->flags & SC_CAN_FRAME_FLAG_DRP) ? "dropped" : "transmitted");


	timestamp_us = usb_priv->host_to_dev32(txr->timestamp_us);
	sc_usb_ktime_from_us(usb_priv, timestamp_us, &hwts);

	spin_lock_irqsave(&usb_priv->tx_lock, flags);

	SC_DEBUG_VERIFY(usb_priv->tx_echo_skb_available_count < net_priv->can.echo_skb_max, goto unlock);
#if DEBUG
	{
		unsigned int i;

		for (i = 0; i < usb_priv->tx_echo_skb_available_count; ++i)
			SC_VERIFY(usb_priv->tx_echo_skb_available_ptr[i] != echo_skb_index, goto unlock);
	}
#endif
	usb_priv->tx_echo_skb_available_ptr[usb_priv->tx_echo_skb_available_count++] = echo_skb_index;

	if (txr->flags & SC_CAN_FRAME_FLAG_DRP) {
		// remove echo skb
		++netdev->stats.tx_dropped;
		can_free_echo_skb(netdev, echo_skb_index);
	} else {
		// place echo skb
		struct sk_buff *skb = net_priv->can.echo_skb[echo_skb_index];
		struct canfd_frame *cf = NULL;

		SC_DEBUG_VERIFY(skb, goto unlock);
		cf = (struct canfd_frame *)skb->data;

		++netdev->stats.tx_packets;
		if (!(cf->can_id & CAN_RTR_FLAG))
			netdev->stats.tx_bytes += cf->len;

		skb_hwtstamps(skb)->hwtstamp = hwts;
		can_get_echo_skb(netdev, echo_skb_index);
	}

	if (usb_priv->tx_echo_skb_available_count == 1
		&& usb_priv->tx_urb_available_count)
		netif_wake_queue(netdev);

#if DEBUG
unlock:
#endif
	spin_unlock_irqrestore(&usb_priv->tx_lock, flags);

	return 0;
}

static int sc_usb_process_msg(struct sc_usb_priv *usb_priv, struct sc_msg_header *hdr)
{
	switch (hdr->id) {
	case SC_MSG_CAN_STATUS:
		return sc_usb_process_can_status(usb_priv, (struct sc_msg_can_status *)hdr);
	case SC_MSG_CAN_RX:
		return sc_usb_process_can_rx(usb_priv, (struct sc_msg_can_rx *)hdr);
	case SC_MSG_CAN_TXR:
		return sc_usb_process_can_txr(usb_priv, (struct sc_msg_can_txr *)hdr);
	case SC_MSG_CAN_ERROR:
		return sc_usb_process_can_error(usb_priv, (struct sc_msg_can_error *)hdr);
	default:
		netdev_dbg(usb_priv->netdev, "skip unknown msg id=%#02x len=%u\n", hdr->id, hdr->len);
		/* unhandled messages are expected as the protocol evolves */
		return -EINVAL;
	}
}

static void sc_usb_process_rx_buffer(struct sc_usb_priv *usb_priv, u8 * const urb_data_ptr, unsigned int urb_data_size)
{
	u8 const *sptr = urb_data_ptr;
	u8 const *eptr = sptr + urb_data_size;
	u8 const *mptr = NULL;
	int error = 0;


	SC_ASSERT(usb_priv);
	SC_ASSERT(urb_data_ptr);
	SC_ASSERT(urb_data_size);
	SC_ASSERT(urb_data_size <= usb_priv->msg_buffer_size);

//		if (net_ratelimit())
//			netdev_dbg(usb_priv->netdev, "rx process msg buffer of %u bytes\n", (unsigned)(eptr-sptr));

	for (mptr = sptr; mptr + SC_MSG_CAN_LEN_MULTIPLE <= eptr; ) {
		struct sc_msg_header *hdr = (struct sc_msg_header *)mptr;

		if (unlikely(hdr->id == SC_MSG_EOF || !hdr->len)) {
			if (unlikely(mptr == sptr)) {
				if (net_ratelimit())
					netdev_dbg(usb_priv->netdev, "EOF @ 0\n");
			}
			mptr = eptr;
			break;
		}

		if (unlikely(hdr->len % SC_MSG_CAN_LEN_MULTIPLE)) {
			netdev_dbg(usb_priv->netdev, "offset=%u: invalid msg size len=%u\n", (unsigned int)(mptr - sptr), hdr->len);
			goto error_exit;
		}


		if (unlikely(mptr + hdr->len > eptr)) {
			netdev_err(usb_priv->netdev, "offset=%u: msg len=%u exceeds buffer len=%u\n", (unsigned int)(mptr - sptr), hdr->len, (unsigned int)(eptr - sptr));
			goto error_exit;
		}


		mptr += hdr->len;

		error = sc_usb_process_msg(usb_priv, hdr);
		if (unlikely(error))
			goto error_exit;
	}

	return;

error_exit:
	print_hex_dump(KERN_ERR, SC_NAME " ", DUMP_PREFIX_NONE, 16, 1, urb_data_ptr, urb_data_size, false);
}

static void sc_usb_tx_completed(struct urb *urb)
{
	struct sc_urb_data *urb_data = NULL;
	struct sc_usb_priv *usb_priv = NULL;
	unsigned long flags = 0;
	unsigned int index = 0;

#if DEBUG
	unsigned int i;
#endif

	SC_ASSERT(urb);
	SC_ASSERT(urb->context);

	urb_data = (struct sc_urb_data *)urb->context;
	usb_priv = urb_data->usb_priv;
	index = urb_data - usb_priv->tx_urb_ptr;
	//netdev_dbg(usb_priv->netdev, "tx URB %u back\n", index);
	SC_DEBUG_VERIFY(index < usb_priv->tx_urb_count, return);
	(void)index;

	if (unlikely(!netif_device_present(usb_priv->netdev)))
		return;

	spin_lock_irqsave(&usb_priv->tx_lock, flags);

	SC_DEBUG_VERIFY(usb_priv->tx_urb_available_count < usb_priv->tx_urb_count, goto unlock);
#if DEBUG
	for (i = 0; i < usb_priv->tx_urb_available_count; ++i)
		SC_VERIFY(index != usb_priv->tx_urb_available_ptr[i], goto unlock);

#endif

	usb_priv->tx_urb_available_ptr[usb_priv->tx_urb_available_count++] = index;

	// wake queue?
	if (usb_priv->tx_urb_available_count == 1
		&& usb_priv->tx_echo_skb_available_count)
		netif_wake_queue(usb_priv->netdev);

	switch (urb->status) {
	case 0:
		break;
	case -ENOENT:
	case -EILSEQ:
	case -ESHUTDOWN:
	case -EPIPE:
	case -ETIME:
		// known 'device gone' error codes
		break;
	default:
		netdev_info(usb_priv->netdev, "tx URB status %d\n", urb->status);
		break;
	}

#if DEBUG
unlock:
#endif
	spin_unlock_irqrestore(&usb_priv->tx_lock, flags);
}

static void sc_usb_rx_completed(struct urb *urb)
{
	struct sc_urb_data *urb_data = NULL;
	struct sc_usb_priv *usb_priv = NULL;
//	unsigned long flags = 0;
	int rc = 0;
#ifdef DEBUG
	int threads = 0;
#endif
	u8 opened = 0;

	SC_ASSERT(urb);

	urb_data = (struct sc_urb_data *)urb->context;
	SC_ASSERT(urb_data);
	usb_priv = urb_data->usb_priv;
	SC_ASSERT(usb_priv);



	if (unlikely(!netif_device_present(usb_priv->netdev)))
		return;

	/* protect against races with netdev open/close */
	opened = READ_ONCE(usb_priv->opened);
	if (unlikely(!opened))
		return;

#ifdef DEBUG
	/* could not determine any concurrent access though atomic counter */
	threads = atomic_inc_return(&usb_priv->rx_enter);
	if (unlikely(threads > 1)) {
		dev_err(&usb_priv->intf->dev, "%d threads in %s\n", threads, __func__);
	}
#endif

	// spin_lock_irqsave(&usb_priv->rx_lock, flags);

	// if (unlikely(!usb_priv->opened))
	// 	dev_dbg(&usb_priv->intf->dev, "netdev not opened\n");


	if (likely(urb->status == 0)) {
		if (likely(urb->actual_length > 0))
			sc_usb_process_rx_buffer(usb_priv, (u8 *)urb->transfer_buffer, (unsigned int)urb->actual_length);

		//SC_DEBUG_VERIFY(urb->transfer_buffer_length == usb_priv->msg_buffer_size, goto unlock);
		SC_ASSERT(urb->transfer_buffer_length == usb_priv->msg_buffer_size);
		rc = usb_submit_urb(urb, GFP_ATOMIC);
		//dev_dbg(&usb_priv->intf->dev, "rx URB index %u\n", (unsigned int)(urb_data - usb_priv->rx_urb_ptr));

		if (unlikely(rc)) {
			dev_dbg(&usb_priv->intf->dev, "rx URB index %u\n", (unsigned int)(urb_data - usb_priv->rx_urb_ptr));
			switch (rc) {
			case -ENOENT:
			case -ETIME:
				// dev_dbg(&usb_priv->intf->dev, "rx URB resubmit failed: %d\n", rc);
				break;
			case -ENODEV:
				// oopsie daisy, USB device gone, remove can dev
				netif_device_detach(usb_priv->netdev);
				break;
			default:
				dev_err(&usb_priv->intf->dev, "rx URB resubmit failed: %d\n", rc);
				break;
			}
		}
	} else {
		dev_dbg(&usb_priv->intf->dev, "bad URB status %d on rx URB index %u\n", urb->status, (unsigned int)(urb_data - usb_priv->rx_urb_ptr));
		switch (urb->status) {
		case -ENOENT:
		case -EILSEQ:
		case -ESHUTDOWN:
		case -EPIPE:
		case -ETIME:
			break;
		default:
			// dev_info(&usb_priv->intf->dev, "rx URB status %d\n", urb->status);
			break;
		}
	}

#ifdef DEBUG
	atomic_dec_return(&usb_priv->rx_enter);
#endif

// #if DEBUG
// unlock:
// #endif
// 	spin_unlock_irqrestore(&usb_priv->rx_lock, flags);
}

static int sc_usb_submit_rx_urbs(struct sc_usb_priv *usb_priv)
{
	int rc = 0;
	unsigned int i = 0;


	SC_ASSERT(usb_priv);
	SC_ASSERT(usb_priv->rx_urb_count == 0 || NULL != usb_priv->rx_urb_ptr);

	for (i = 0; i < usb_priv->rx_urb_count; ++i) {
		struct sc_urb_data *urb_data = &usb_priv->rx_urb_ptr[i];

		rc = usb_submit_urb(urb_data->urb, GFP_KERNEL);
		if (unlikely(rc)) {
			netdev_dbg(usb_priv->netdev, "rx URB submit index %u failed: %d\n", i, rc);
			break;
		}

		netdev_dbg(usb_priv->netdev, "submit rx URB index %u\n", i);
	}

	return rc;
}

static int sc_apply_configuration(struct sc_usb_priv *usb_priv)
{
	struct sc_net_priv *net_priv = netdev_priv(usb_priv->netdev);
	struct sc_msg_features *feat = (struct sc_msg_features *)usb_priv->tx_cmd_buffer;

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

	netdev_dbg(usb_priv->netdev, "clear features\n");
	rc = sc_cmd_send_receive(usb_priv, sizeof(*feat));
	if (rc)
		return rc;

	// set target features
	feat->op = SC_FEAT_OP_OR;
	feat->arg = usb_priv->host_to_dev32(features);

	netdev_dbg(usb_priv->netdev, "add features %#x\n", features);
	rc = sc_cmd_send_receive(usb_priv, sizeof(*feat));
	if (rc)
		return rc;

	return 0;
}

static int sc_usb_netdev_open(struct net_device *netdev)
{
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct sc_usb_priv *usb_priv = net_priv->usb;
	// unsigned long flags = 0;
	int rc = 0;

	SC_ASSERT(usb_priv->tx_urb_available_count == usb_priv->tx_urb_count);
	SC_ASSERT(usb_priv->tx_echo_skb_available_count == net_priv->can.echo_skb_max);

	rc = open_candev(netdev);
	if (rc) {
		netdev_dbg(netdev, "candev open failed: %d\n", rc);
		goto fail;
	}

	net_priv->can.state = CAN_STATE_ERROR_ACTIVE;

	// spin_lock_irqsave(&usb_priv->rx_lock, flags);
	{
		WRITE_ONCE(usb_priv->opened, 1);
#ifdef DEBUG
		atomic_set(&usb_priv->rx_enter, 0);
#endif
		rc = sc_usb_submit_rx_urbs(usb_priv);
	}
	// spin_unlock_irqrestore(&usb_priv->rx_lock, flags);

	if (rc) {
		netdev_dbg(netdev, "submit rx urbs failed: %d\n", rc);
		goto fail;
	}

	rc = sc_apply_configuration(usb_priv);
	if (rc) {
		netdev_dbg(netdev, "apply configuration failed: %d\n", rc);
		goto fail;
	}

	rc = sc_usb_cmd_bus(usb_priv, 1);
	if (rc) {
		netdev_dbg(netdev, "bus on failed: %d\n", rc);
		goto fail;
	}

	netdev_dbg(netdev, "start queue\n");
	netif_start_queue(netdev);

out:
	return rc;

fail:
	netdev_dbg(netdev, "%s failed, closing device\n", __func__);
	sc_usb_netdev_close(netdev);
	goto out;
}

static void sc_usb_fill_tx(
	struct sk_buff const *skb,
	struct sc_usb_priv *usb_priv,
	u8 track_id,
	struct sc_msg_can_tx *tx,
	u8 tx_len)
{
	struct canfd_frame const *cf = (struct canfd_frame const *)skb->data;

	// netdev_dbg(usb_priv->netdev, "tx can_id %x\n", CAN_EFF_MASK & cf->can_id);

	tx->id = SC_MSG_CAN_TX;
	tx->len = tx_len;
	tx->track_id = track_id;
	tx->can_id = usb_priv->host_to_dev32(CAN_EFF_MASK & cf->can_id);
	tx->dlc = can_len2dlc(cf->len);

	tx->flags = 0;

	if (CAN_EFF_FLAG & cf->can_id)
		tx->flags |= SC_CAN_FRAME_FLAG_EXT;

	if (can_is_canfd_skb(skb)) {
		tx->flags |= SC_CAN_FRAME_FLAG_FDF;
		if (cf->flags & CANFD_BRS)
			tx->flags |= SC_CAN_FRAME_FLAG_BRS;
		if (cf->flags & CANFD_ESI)
			tx->flags |= SC_CAN_FRAME_FLAG_ESI;

		memcpy(tx->data, cf->data, cf->len);
	} else {
		if (cf->can_id & CAN_RTR_FLAG)
			tx->flags |= SC_CAN_FRAME_FLAG_RTR;
		else
			memcpy(tx->data, cf->data, cf->len);
	}
}

static inline unsigned int sc_usb_tx_len(struct sk_buff const *skb)
{
	struct canfd_frame const *cf = NULL;
	unsigned int len = (unsigned int)sizeof(struct sc_msg_can_tx);

	SC_ASSERT(skb);

	cf = (struct canfd_frame const *)skb->data;

	if (0 == (cf->can_id & CAN_RTR_FLAG))
		len += cf->len;

	return round_up(len, SC_MSG_CAN_LEN_MULTIPLE);
}

static
netdev_tx_t
sc_usb_netdev_start_xmit(struct sk_buff *skb, struct net_device *netdev)
{
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct sc_usb_priv *usb_priv = net_priv->usb;
	unsigned long flags = 0;
	netdev_tx_t rc = NETDEV_TX_OK;
	unsigned int tx_len = 0;


	if (unlikely(can_dropped_invalid_skb(netdev, skb)))
		return NETDEV_TX_OK;

	tx_len = sc_usb_tx_len(skb);

	spin_lock_irqsave(&usb_priv->tx_lock, flags);

	if (usb_priv->tx_echo_skb_available_count
		&& usb_priv->tx_urb_available_count) {

		struct sc_urb_data *urb_data = NULL;
		struct sc_msg_can_tx *tx = NULL;
		unsigned int urb_index = -1;
		unsigned int echo_skb_index = -1;
		int error = 0;
		bool zlp_condition = false;


		echo_skb_index = usb_priv->tx_echo_skb_available_ptr[--usb_priv->tx_echo_skb_available_count];
		SC_DEBUG_VERIFY(echo_skb_index < net_priv->can.echo_skb_max, goto unlock);
		can_put_echo_skb(skb, netdev, echo_skb_index);


		urb_index = usb_priv->tx_urb_available_ptr[--usb_priv->tx_urb_available_count];
		SC_DEBUG_VERIFY(urb_index < usb_priv->tx_urb_count, goto unlock);

		urb_data = &usb_priv->tx_urb_ptr[urb_index];
		tx = urb_data->mem;
		sc_usb_fill_tx(skb, usb_priv, echo_skb_index, tx, tx_len);

		//SC_DEBUG_VERIFY(tx->len == tx_len, goto unlock);
		//SC_DEBUG_VERIFY(tx->track_id == echo_skb_index, goto unlock);

		zlp_condition =
			tx_len < usb_priv->msg_buffer_size &&
			usb_priv->ep_size < usb_priv->msg_buffer_size &&
			(tx_len % usb_priv->ep_size) == 0;

		if (unlikely(zlp_condition)) {
			*(u32*)(((u8*)urb_data->mem) + tx_len) = 0;
			tx_len += 4;
		}

		urb_data->urb->transfer_buffer_length = tx_len;

		error = usb_submit_urb(urb_data->urb, GFP_ATOMIC);

		if (error) {
			// remove echo
			can_free_echo_skb(netdev, echo_skb_index);
			usb_priv->tx_echo_skb_available_ptr[usb_priv->tx_echo_skb_available_count++] = echo_skb_index;
			// put urb back
			usb_priv->tx_urb_available_ptr[usb_priv->tx_urb_available_count++] = urb_index;

			if (-ENODEV == error)
				netif_device_detach(netdev);
			else
				netdev_warn(netdev, "URB submit failed: %d\n", error);
		} else {
			//netdev_dbg(netdev, "tx URB %u echo %u\n", urb_index, echo_skb_index);
		}
	} else {
		//netdev_dbg(netdev, "tx stop queue\n");
		netif_stop_queue(netdev);
		rc = NETDEV_TX_BUSY;
	}

#if DEBUG
unlock:
#endif
	spin_unlock_irqrestore(&usb_priv->tx_lock, flags);

	return rc;
}

static int sc_usb_can_set_bittiming(struct net_device *netdev)
{
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct sc_usb_priv *usb_priv = net_priv->usb;
	struct sc_msg_bittiming *bt = (struct sc_msg_bittiming *)usb_priv->tx_cmd_buffer;

	memset(bt, 0, sizeof(*bt));
	bt->id = SC_MSG_NM_BITTIMING;
	bt->len = sizeof(*bt);
	bt->brp = usb_priv->host_to_dev16(net_priv->can.bittiming.brp);
	bt->sjw = net_priv->can.bittiming.sjw;
	bt->tseg1 = usb_priv->host_to_dev16(net_priv->can.bittiming.prop_seg + net_priv->can.bittiming.phase_seg1);
	bt->tseg2 = net_priv->can.bittiming.phase_seg2;
	netdev_dbg(usb_priv->netdev, "nominal brp=%lu sjw=%lu tseg1=%lu, tseg2=%lu bitrate=%lu\n",
		(unsigned long)net_priv->can.bittiming.brp, (unsigned long)net_priv->can.bittiming.sjw,
		(unsigned long)(net_priv->can.bittiming.prop_seg + net_priv->can.bittiming.phase_seg1),
		(unsigned long)net_priv->can.bittiming.phase_seg2, (unsigned long)net_priv->can.bittiming.bitrate);

	netdev_dbg(usb_priv->netdev, "set nomimal bittiming\n");
	return sc_cmd_send_receive(usb_priv, sizeof(*bt));
}

static int sc_usb_can_set_data_bittiming(struct net_device *netdev)
{
	struct sc_net_priv *net_priv = netdev_priv(netdev);
	struct sc_usb_priv *usb_priv = net_priv->usb;
	struct sc_msg_bittiming *bt = (struct sc_msg_bittiming *)usb_priv->tx_cmd_buffer;

	memset(bt, 0, sizeof(*bt));
	bt->id = SC_MSG_DT_BITTIMING;
	bt->len = sizeof(*bt);
	bt->brp = usb_priv->host_to_dev16(net_priv->can.data_bittiming.brp);
	bt->sjw = net_priv->can.data_bittiming.sjw;
	bt->tseg1 = usb_priv->host_to_dev16(net_priv->can.data_bittiming.prop_seg + net_priv->can.data_bittiming.phase_seg1);
	bt->tseg2 = net_priv->can.data_bittiming.phase_seg2;
	netdev_dbg(usb_priv->netdev, "data brp=%lu sjw=%lu tseg1=%lu, tseg2=%lu bitrate=%lu\n",
		(unsigned long)net_priv->can.data_bittiming.brp, (unsigned long)net_priv->can.data_bittiming.sjw,
		(unsigned long)(net_priv->can.data_bittiming.prop_seg + net_priv->can.data_bittiming.phase_seg1),
		(unsigned long)net_priv->can.data_bittiming.phase_seg2, (unsigned long)net_priv->can.data_bittiming.bitrate);

	netdev_dbg(usb_priv->netdev, "set data bittiming\n");
	return sc_cmd_send_receive(usb_priv, sizeof(*bt));
}

static int sc_usb_can_get_berr_counter(const struct net_device *netdev, struct can_berr_counter *bec)
{
	struct sc_net_priv *priv = netdev_priv(netdev);
	*bec = priv->bec;
	return 0;
}

static const struct net_device_ops sc_usb_netdev_ops = {
	.ndo_open = &sc_usb_netdev_open,
	.ndo_stop = &sc_usb_netdev_close,
	.ndo_start_xmit = &sc_usb_netdev_start_xmit,
	.ndo_change_mtu = &can_change_mtu,
};


static void sc_usb_cleanup_urbs(struct sc_usb_priv *usb_priv)
{
	struct usb_device *udev = NULL;
	unsigned int i = 0;

	SC_ASSERT(usb_priv);
	SC_ASSERT(usb_priv->intf);

	udev = interface_to_usbdev(usb_priv->intf);

	if (usb_priv->rx_urb_ptr) {
		for (i = 0; i < usb_priv->rx_urb_count; ++i) {
			struct sc_urb_data *urb_data = &usb_priv->rx_urb_ptr[i];

			usb_free_coherent(udev, usb_priv->msg_buffer_size, urb_data->mem, urb_data->dma_addr);
			usb_free_urb(urb_data->urb);
		}
		usb_priv->rx_urb_count = 0;
		kfree(usb_priv->rx_urb_ptr);
		usb_priv->rx_urb_ptr = NULL;
	}

	if (usb_priv->tx_urb_ptr) {
		for (i = 0; i < usb_priv->tx_urb_count; ++i) {
			struct sc_urb_data *urb_data = &usb_priv->tx_urb_ptr[i];

			usb_free_coherent(udev, usb_priv->msg_buffer_size, urb_data->mem, urb_data->dma_addr);
			usb_free_urb(urb_data->urb);
		}
		usb_priv->tx_urb_count = 0;
		kfree(usb_priv->tx_urb_ptr);
		usb_priv->tx_urb_ptr = NULL;
	}

	kfree(usb_priv->tx_urb_available_ptr);
	usb_priv->tx_urb_available_ptr = NULL;
	usb_priv->tx_urb_available_count = 0;
}

static void sc_usb_netdev_uninit(struct sc_usb_priv *usb_priv)
{
	if (usb_priv->netdev) {
		if (usb_priv->registered)
			unregister_candev(usb_priv->netdev);

		free_candev(usb_priv->netdev);
		usb_priv->netdev = NULL;
	}

	kfree(usb_priv->tx_cmd_buffer);
	usb_priv->tx_cmd_buffer = NULL;
	usb_priv->rx_cmd_buffer = NULL;

	kfree(usb_priv->tx_echo_skb_available_ptr);
	usb_priv->tx_echo_skb_available_ptr = NULL;

	sc_usb_cleanup_urbs(usb_priv);
}


static int sc_usb_alloc_urbs(struct sc_usb_priv *usb_priv)
{
	struct usb_device *udev = NULL;
	struct device *dev = NULL;
	unsigned int rx_pipe = 0;
	unsigned int rx_urbs = 0;
	unsigned int tx_pipe = 0;
	unsigned int tx_urbs = 0;
	unsigned int tx_urb_flags = URB_NO_TRANSFER_DMA_MAP;
	size_t bytes = 0;

	SC_ASSERT(usb_priv);
	SC_ASSERT(usb_priv->intf);

	udev = interface_to_usbdev(usb_priv->intf);

	dev = &usb_priv->intf->dev;

	rx_pipe = usb_rcvbulkpipe(udev, usb_priv->msg_epp);
	tx_pipe = usb_sndbulkpipe(udev, usb_priv->msg_epp);
	bytes = usb_priv->msg_buffer_size;
	rx_urbs = usb_priv->static_rx_fifo_size;
	tx_urbs = usb_priv->static_tx_fifo_size;
	tx_urb_flags |= usb_priv->msg_buffer_size > usb_priv->ep_size ? URB_ZERO_PACKET : 0;

	usb_priv->rx_urb_count = 0;
	usb_priv->tx_urb_count = 0;
	usb_priv->rx_urb_ptr = NULL;
	usb_priv->tx_urb_ptr = NULL;

	usb_priv->rx_urb_ptr = kcalloc(rx_urbs, sizeof(*usb_priv->rx_urb_ptr), GFP_KERNEL);
	if (!usb_priv->rx_urb_ptr)
		goto err_no_mem;

	usb_priv->tx_urb_ptr = kcalloc(tx_urbs, sizeof(*usb_priv->tx_urb_ptr), GFP_KERNEL);
	if (!usb_priv->tx_urb_ptr)
		goto err_no_mem;

	usb_priv->tx_urb_available_ptr = kcalloc(tx_urbs, sizeof(*usb_priv->tx_urb_available_ptr), GFP_KERNEL);
	if (!usb_priv->tx_urb_available_ptr)
		goto err_no_mem;

	for (usb_priv->rx_urb_count = 0; usb_priv->rx_urb_count < rx_urbs; ++usb_priv->rx_urb_count) {
		struct sc_urb_data *urb_data = &usb_priv->rx_urb_ptr[usb_priv->rx_urb_count];

		urb_data->usb_priv = usb_priv;
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

	if (usb_priv->rx_urb_count == 0) {
		dev_err(&usb_priv->intf->dev, "no rx URBs\n");
		goto err_no_mem;
	}

	if (usb_priv->rx_urb_count < rx_urbs)
		dev_warn(&usb_priv->intf->dev, "only %u/%u rx URBs allocated\n", usb_priv->rx_urb_count, rx_urbs);


	for (usb_priv->tx_urb_count = 0; usb_priv->tx_urb_count < tx_urbs; ++usb_priv->tx_urb_count) {
		struct sc_urb_data *urb_data = &usb_priv->tx_urb_ptr[usb_priv->tx_urb_count];

		urb_data->usb_priv = usb_priv;
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
		urb_data->urb->transfer_flags |= tx_urb_flags;

		usb_priv->tx_urb_available_ptr[usb_priv->tx_urb_count] = usb_priv->tx_urb_count;
	}

	if (usb_priv->tx_urb_count == 0) {
		dev_err(dev, "no tx URBs\n");
		goto err_no_mem;
	}

	if (usb_priv->tx_urb_count < tx_urbs)
		dev_warn(dev, "only %u/%u tx URBs allocated\n", usb_priv->tx_urb_count, tx_urbs);

	usb_priv->tx_urb_available_count = usb_priv->tx_urb_count;

	return 0;

err_no_mem:
	sc_usb_cleanup_urbs(usb_priv);

	return -ENOMEM;
}

static int sc_usb_netdev_init(struct sc_usb_priv *usb_priv)
{
	struct sc_net_priv *net_priv = NULL;
	int rc = 0;
	unsigned int i = 0;

	SC_ASSERT(usb_priv);

	spin_lock_init(&usb_priv->tx_lock);
	//spin_lock_init(&usb_priv->rx_lock);

	usb_priv->tx_cmd_buffer = kmalloc(2 * usb_priv->cmd_buffer_size, GFP_KERNEL);
	if (!usb_priv->tx_cmd_buffer) {
		rc = -ENOMEM;
		goto fail;
	}

	/* rx cmd buffer is part of tx cmd buffer */
	usb_priv->rx_cmd_buffer = usb_priv->tx_cmd_buffer + usb_priv->cmd_buffer_size;

	usb_priv->tx_echo_skb_available_count = usb_priv->static_tx_fifo_size;
	usb_priv->tx_echo_skb_available_ptr = kmalloc_array(usb_priv->tx_echo_skb_available_count, sizeof(*usb_priv->tx_echo_skb_available_ptr), GFP_KERNEL);
	if (!usb_priv->tx_echo_skb_available_ptr) {
		rc = -ENOMEM;
		goto fail;
	}

	for (i = 0; i < usb_priv->tx_echo_skb_available_count; ++i)
		usb_priv->tx_echo_skb_available_ptr[i] = i;

	rc = sc_usb_alloc_urbs(usb_priv);
	if (rc)
		goto fail;


	usb_priv->netdev = alloc_candev(sizeof(*net_priv), usb_priv->static_tx_fifo_size);
	if (!usb_priv->netdev) {
		dev_err(&usb_priv->intf->dev, "candev alloc failed\n");
		rc = -ENOMEM;
		goto fail;
	}


	usb_priv->netdev->flags |= IFF_ECHO;
	usb_priv->netdev->netdev_ops = &sc_usb_netdev_ops;
	SET_NETDEV_DEV(usb_priv->netdev, &usb_priv->intf->dev);

	net_priv = netdev_priv(usb_priv->netdev);
	net_priv->usb = usb_priv;

	// Next line, if uncommented, effectively disables CAN-FD mode
	// net_priv->can.ctrlmode_static = usb_priv->ctrlmode_static;
	net_priv->can.ctrlmode_supported = usb_priv->ctrlmode_supported | usb_priv->ctrlmode_static;
	net_priv->can.state = CAN_STATE_STOPPED;
	net_priv->can.clock.freq = usb_priv->can_clock_hz;

	// net_priv->can.do_set_mode = &sc_usb_can_set_mode;
	net_priv->can.do_get_berr_counter = &sc_usb_can_get_berr_counter;
	net_priv->can.bittiming_const = &usb_priv->nominal;
	net_priv->can.do_set_bittiming = &sc_usb_can_set_bittiming;

	if (usb_priv->ctrlmode_supported & CAN_CTRLMODE_FD) {
		net_priv->can.data_bittiming_const = &usb_priv->data;
		net_priv->can.do_set_data_bittiming = &sc_usb_can_set_data_bittiming;
	}

	rc = register_candev(usb_priv->netdev);
	if (rc) {
		dev_err(&usb_priv->intf->dev, "candev registration failed\n");
		goto fail;
	}

	usb_priv->registered = 1;
	netdev_dbg(usb_priv->netdev, "candev registered\n");

out:
	return rc;

fail:
	sc_usb_netdev_uninit(usb_priv);
	goto out;
}


static void sc_usb_cleanup(struct sc_usb_priv *priv)
{
	if (priv) {
		sc_usb_netdev_uninit(priv);
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

static int sc_usb_probe_prelim(struct usb_interface *intf, u16 *ep_size, u8 *cmd_epp, u8 *msg_epp)
{
	struct usb_host_endpoint *ep = NULL;

	SC_ASSERT(intf);
	SC_ASSERT(ep_size);
	SC_ASSERT(cmd_epp);
	SC_ASSERT(msg_epp);

	// ensure interface is set
	if (unlikely(!intf->cur_altsetting))
		return -ENODEV;

	// we want a VENDOR interface
	if (intf->cur_altsetting->desc.bInterfaceClass != USB_CLASS_VENDOR_SPEC) {
		dev_dbg(&intf->dev, "not a vendor interface: %#02x\n", intf->cur_altsetting->desc.bInterfaceClass);
		return -ENODEV;
	}

	// must have at least two endpoint pairs
	dev_dbg(&intf->dev, "device has %u eps\n", intf->cur_altsetting->desc.bNumEndpoints);
	if (unlikely(intf->cur_altsetting->desc.bNumEndpoints / 2 < 2))
		return -ENODEV;

	ep = intf->cur_altsetting->endpoint;

	// endpoint must be bulk
	if (unlikely(usb_endpoint_type(&ep->desc) != USB_ENDPOINT_XFER_BULK))
		return -ENODEV;

	*ep_size = le16_to_cpu(ep->desc.wMaxPacketSize);
	dev_dbg(&intf->dev, "ep size %u\n", *ep_size);

	// endpoint too small?
	if (unlikely(*ep_size < SC_MIN_SUPPORTED_TRANSFER_SIZE))
		return -ENODEV;

	*cmd_epp = usb_endpoint_num(&ep->desc);
	dev_dbg(&intf->dev, "cmd ep num %u\n", *cmd_epp);

	*msg_epp = usb_endpoint_num(&(ep + 2)->desc);
	dev_dbg(&intf->dev, "msg ep num %u\n", *msg_epp);

	return 0;
}


static int sc_usb_probe_dev(struct sc_usb_priv *usb_priv)
{
	struct sc_msg_req *req = NULL;
	struct sc_msg_hello *device_hello = NULL;
	struct sc_msg_dev_info *device_info = NULL;
	struct sc_msg_can_info *can_info = NULL;
	struct usb_device *udev = NULL;
	void *tx_buf = NULL;
	void *rx_buf = NULL;
	char serial_str[1 + sizeof(device_info->sn_bytes)*2];
	char name_str[1 + sizeof(device_info->name_bytes)];
	unsigned int i = 0;
	const unsigned int can_fd_msg_transfer_size = round_up(
		CANFD_MAX_DLEN + max_t(
			unsigned int,
			sizeof(struct sc_msg_can_rx),
			sizeof(struct sc_msg_can_tx)),
		SC_MIN_SUPPORTED_TRANSFER_SIZE);
	int rc = 0;
	int actual_len = 0;


	SC_ASSERT(usb_priv);
	SC_ASSERT(usb_priv->intf);

	udev = interface_to_usbdev(usb_priv->intf);

	tx_buf = kmalloc(usb_priv->ep_size, GFP_KERNEL);
	if (!tx_buf) {
		rc = -ENOMEM;
		goto cleanup;
	}

	rx_buf = kmalloc(usb_priv->ep_size, GFP_KERNEL);
	if (!rx_buf) {
		rc = -ENOMEM;
		goto cleanup;
	}

	req = tx_buf;
	memset(req, 0, sizeof(*req));
	req->id = SC_MSG_HELLO_DEVICE;
	req->len = sizeof(*req);


	dev_dbg(&usb_priv->intf->dev, "sending SC_MSG_HELLO_DEVICE on %02x\n", usb_priv->cmd_epp);
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, usb_priv->cmd_epp), req,
			sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto cleanup;

	/* wait for response */
	dev_dbg(&usb_priv->intf->dev, "waiting for SC_MSG_HELLO_HOST\n");
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, usb_priv->cmd_epp), rx_buf, usb_priv->ep_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto cleanup;

	device_hello = rx_buf;

	if (actual_len < 0 ||
		(size_t)actual_len < sizeof(struct sc_msg_hello) ||
		device_hello->id != SC_MSG_HELLO_HOST ||
		device_hello->len < sizeof(struct sc_msg_hello)) {
		/* no dice */
		rc = -ENODEV;
		goto cleanup;
	}

	usb_priv->cmd_buffer_size = ntohs(device_hello->cmd_buffer_size);

	if (device_hello->proto_version == 0 ||
		usb_priv->cmd_buffer_size < SC_MIN_SUPPORTED_TRANSFER_SIZE) {
		dev_err(&usb_priv->intf->dev,
			"badly configured device: proto_version=%u cmd_buffer_size=%u\n",
			device_hello->proto_version, usb_priv->cmd_buffer_size);
		rc = -ENODEV;
		goto cleanup;
	}

	if (device_hello->proto_version > SC_VERSION) {
		dev_info(&usb_priv->intf->dev, "device proto version %u exceeds supported version %u\n",
			device_hello->proto_version, SC_VERSION);
		rc = -ENODEV;
		goto cleanup;
	}

	/* At this point we are fairly confident we are dealing with the genuine article. */
	dev_info(
		&usb_priv->intf->dev,
		"device proto version %u, %s endian, cmd buffer of %u bytes\n",
		device_hello->proto_version, device_hello->byte_order == SC_BYTE_ORDER_LE ? "little" : "BIG",
		usb_priv->cmd_buffer_size);

	if (device_hello->byte_order == SC_NATIVE_BYTE_ORDER) {
		usb_priv->host_to_dev16 = &sc_usb_nop16;
		usb_priv->host_to_dev32 = &sc_usb_nop32;
	} else {
		usb_priv->host_to_dev16 = &sc_usb_swap16;
		usb_priv->host_to_dev32 = &sc_usb_swap32;
	}

	// realloc cmd rx buffer if device reports larger cap
	if (usb_priv->ep_size < usb_priv->cmd_buffer_size)
		if (ksize(rx_buf) < usb_priv->cmd_buffer_size) {
			kfree(rx_buf);
			rx_buf = kmalloc(usb_priv->cmd_buffer_size, GFP_KERNEL);
			if (!rx_buf) {
				rc = -ENOMEM;
				goto cleanup;
			}
		}

	req->id = SC_MSG_DEVICE_INFO;
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, usb_priv->cmd_epp), req, sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto cleanup;

	/* wait for response */
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, usb_priv->cmd_epp), rx_buf, usb_priv->cmd_buffer_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto cleanup;

	device_info = rx_buf;

	if (unlikely(actual_len < 0 ||
		(size_t)actual_len < sizeof(*device_info) ||
		device_info->id != SC_MSG_DEVICE_INFO ||
		device_info->len < sizeof(*device_info))) {
		dev_err(&usb_priv->intf->dev, "bad reply to SC_MSG_DEVICE_INFO (%d bytes)\n", actual_len);
		rc = -ENODEV;
		goto cleanup;
	}

	usb_priv->feat_perm = usb_priv->host_to_dev16(device_info->feat_perm);
	usb_priv->feat_conf = usb_priv->host_to_dev16(device_info->feat_conf);
	dev_info(&usb_priv->intf->dev, "device features perm=%04x conf=%04x\n",
		usb_priv->feat_perm, usb_priv->feat_conf);


	for (i = 0; i < min_t(size_t, device_info->sn_len, ARRAY_SIZE(serial_str)-1); ++i)
		snprintf(&serial_str[i*2], 3, "%02x", device_info->sn_bytes[i]);

	serial_str[i*2] = 0;

	device_info->name_len = min_t(size_t, device_info->name_len, sizeof(name_str)-1);
	memcpy(name_str, device_info->name_bytes, device_info->name_len);
	name_str[device_info->name_len] = 0;

	dev_info(&usb_priv->intf->dev, "device %s, serial %s, firmware version %u.%u.%u\n",
		name_str, serial_str, device_info->fw_ver_major, device_info->fw_ver_minor, device_info->fw_ver_patch);


	req->id = SC_MSG_CAN_INFO;
	rc = usb_bulk_msg(udev, usb_sndbulkpipe(udev, usb_priv->cmd_epp), req, sizeof(*req), &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto cleanup;

	/* wait for response */
	rc = usb_bulk_msg(udev, usb_rcvbulkpipe(udev, usb_priv->cmd_epp), rx_buf, usb_priv->cmd_buffer_size, &actual_len, SC_USB_TIMEOUT_MS);
	if (rc)
		goto cleanup;

	can_info = rx_buf;

	if (unlikely(actual_len < 0 ||
				 (size_t)actual_len < sizeof(*can_info) ||
				 can_info->id != SC_MSG_CAN_INFO ||
				 can_info->len < sizeof(*can_info))) {
		dev_err(&usb_priv->intf->dev, "bad reply to SC_MSG_CAN_INFO (%d bytes)\n", actual_len);
		rc = -ENODEV;
		goto cleanup;
	}

	usb_priv->can_clock_hz = usb_priv->host_to_dev32(can_info->can_clk_hz);
	usb_priv->static_tx_fifo_size = min_t(u8, can_info->tx_fifo_size, SC_MAX_TX_URBS);
	usb_priv->static_rx_fifo_size = min_t(u8, can_info->rx_fifo_size, SC_MAX_RX_URBS);
	usb_priv->msg_buffer_size = usb_priv->host_to_dev16(can_info->msg_buffer_size);



	dev_info(&usb_priv->intf->dev, "device has CAN msg buffer of %u bytes\n", usb_priv->msg_buffer_size);

	if ((usb_priv->feat_perm & SC_FEATURE_FLAG_FDF) &&
		usb_priv->msg_buffer_size < can_fd_msg_transfer_size) {

		dev_err(
			&usb_priv->intf->dev,
			"device has CAN-FD permanently enabled but its message buffer is "
			"too small for chunked transfer of %u bytes\n", can_fd_msg_transfer_size);
		rc = -ENODEV;
		goto cleanup;
	}


	// nominal bitrate
	strlcpy(usb_priv->nominal.name, SC_NAME, sizeof(usb_priv->nominal.name));
	usb_priv->nominal.tseg1_min = can_info->nmbt_tseg1_min;
	usb_priv->nominal.tseg1_max = usb_priv->host_to_dev16(can_info->nmbt_tseg1_max);
	usb_priv->nominal.tseg2_min = can_info->nmbt_tseg2_min;
	usb_priv->nominal.tseg2_max = can_info->nmbt_tseg2_max;
	usb_priv->nominal.sjw_max = can_info->nmbt_sjw_max;
	usb_priv->nominal.brp_min = can_info->nmbt_brp_min;
	usb_priv->nominal.brp_max = usb_priv->host_to_dev16(can_info->nmbt_brp_max);
	usb_priv->nominal.brp_inc = 1;

	// data bitrate
	strlcpy(usb_priv->data.name, SC_NAME, sizeof(usb_priv->data.name));
	usb_priv->data.tseg1_min = can_info->dtbt_tseg1_min;
	usb_priv->data.tseg1_max = can_info->dtbt_tseg1_max;
	usb_priv->data.tseg2_min = can_info->dtbt_tseg2_min;
	usb_priv->data.tseg2_max = can_info->dtbt_tseg2_max;
	usb_priv->data.sjw_max = can_info->dtbt_sjw_max;
	usb_priv->data.brp_min = can_info->dtbt_brp_min;
	usb_priv->data.brp_max = can_info->dtbt_brp_max;
	usb_priv->data.brp_inc = 1;

	// FIX ME: add code paths for permanently enabled features
	usb_priv->ctrlmode_static |= CAN_CTRLMODE_BERR_REPORTING;

	if (usb_priv->feat_conf & SC_FEATURE_FLAG_FDF) {
		if (usb_priv->msg_buffer_size < can_fd_msg_transfer_size) {
			dev_warn(
				&usb_priv->intf->dev,
				"device supports CAN-FD but its message buffer is too small for chunked "
				"transfer of %u bytes. CAN-FD will not be available.\n", can_fd_msg_transfer_size);
		} else {
			dev_info(&usb_priv->intf->dev, "device supports CAN-FD\n");
			usb_priv->ctrlmode_supported |= CAN_CTRLMODE_FD;
		}
	}

	if (usb_priv->feat_conf & SC_FEATURE_FLAG_MON_MODE) {
		dev_info(&usb_priv->intf->dev, "device supports monitoring mode\n");
		usb_priv->ctrlmode_supported |= CAN_CTRLMODE_LISTENONLY;
	}

	if (usb_priv->feat_conf & SC_FEATURE_FLAG_EXT_LOOP_MODE) {
		dev_info(&usb_priv->intf->dev, "device supports external loopback mode\n");
		usb_priv->ctrlmode_supported |= CAN_CTRLMODE_LOOPBACK;
	}

	if (usb_priv->feat_conf & SC_FEATURE_FLAG_DAR) {
		dev_info(&usb_priv->intf->dev, "device supports one-shot mode\n");
		usb_priv->ctrlmode_supported |= CAN_CTRLMODE_ONE_SHOT;
	}

	if (!((usb_priv->feat_perm | usb_priv->feat_conf) & SC_FEATURE_FLAG_TXR)) {
		dev_err(&usb_priv->intf->dev, "device doesn't support txr feature required by this driver\n");
		rc = -ENODEV;
		goto cleanup;
	}

	rc = sc_usb_netdev_init(usb_priv);
	if (rc)
		goto cleanup;

cleanup:
	kfree(tx_buf);
	kfree(rx_buf);

	return rc;
}

static int sc_usb_probe(struct usb_interface *intf, const struct usb_device_id *id)
{
	struct sc_usb_priv *usb_priv = NULL;
	int rc = 0;
	u16 ep_size = 0;
	u8 cmd_epp = 0;
	u8 msg_epp = 0;

	(void)id; // unused

	rc = sc_usb_probe_prelim(intf, &ep_size, &cmd_epp, &msg_epp);
	if (rc)
		goto err;

	usb_priv = kzalloc(sizeof(*usb_priv), GFP_KERNEL);
	if (!usb_priv) {
		rc = -ENOMEM;
		goto err;
	}

	usb_priv->intf = intf;
	usb_priv->ep_size = ep_size;
	usb_priv->cmd_epp = cmd_epp;
	usb_priv->msg_epp = msg_epp;

	rc = sc_usb_probe_dev(usb_priv);
	if (rc)
		goto err;

	usb_set_intfdata(intf, usb_priv);

out:
	return rc;

err:
	sc_usb_cleanup(usb_priv);
	goto out;
}

MODULE_AUTHOR("Jean Gressmann <jean@0x42.de>");
MODULE_DESCRIPTION("Driver for the SuperCAN family of CAN(-FD) interfaces");
MODULE_LICENSE("GPL v2");



static const struct usb_device_id sc_usb_device_id_table[] = {
	{ USB_DEVICE(0x1d50, 0x5035) },
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
