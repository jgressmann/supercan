/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Jean Gressmann <jean@0x42.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * INTERFACE DESIGN
 *
 * For monetary reasons a signed Windows kernel driver is not an option.
 *
 * Windows 10 supports arbitrary USB devices through the WinUSB driver. In order
 * to make the magic work, each interface exposed by the device must be tagged with
 * a MS OS 2.0 descriptor.
 *
 * https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-os-2-0-descriptors-specification
 *
 * This descriptor can include a GUID which can later on be used to
 * find and open the USB device exposing the interface. This requires no special
 * permissions and can be done from userspace.
 *
 * Since here is no single code path that applications go through (such
 * as a driver for example), sharing a configuration channel on a multi CAN channel
 * USB device has to be handled in userspace. This could be done by a system service
 * since on Windows afaik a device can only be opened by a single application.
 * Userspace code would then talk to the service to gain access to the CAN channel.
 *
 * The solution adopted here sidesteps this whole issue by requiring each CAN channel
 * to have its own configuration channel. This nicely isolates each CAN channel and
 * doesn't require the use of a system service.
 *
 * ENDPOINTS
 *
 * SuperCAN uses bulk endpoints only. Endpoints must be paired (in/out).
 * On an interface, the first endpoint pair must be the configuration channel. If there is
 * another endpoint pair, this endpoint pair is assumed to be the CAN channel. Else CAN
 * shares the configuration channel. This has implications for the application and is
 * current not implemented.
 *
 */

#define SC_NAME "SuperCAN"
#define SC_VERSION 1

#define SC_MSG_HEADER_LEN           sizeof(struct sc_msg_header)
#define SC_MSG_HEADER_ID_OFFSET     0
#define SC_MSG_HEADER_LEN_OFFSET    1

#define SC_MSG_EOF              0x00    ///< Indicates the end of messages in the buffer.
#define SC_MSG_HELLO_DEVICE     0x01    ///< Host -> Device. This is the first message sent to the device. The device MUST respond with SC_MSG_HELLO_HOST *and* cease all CAN bus activity and go off the bus.
#define SC_MSG_HELLO_HOST       0x02    ///< Device -> Host. See SC_MSG_HELLO_DEVICE.
#define SC_MSG_DEVICE_INFO      0x03    ///< Host <-> Device. Query / Send device information.
#define SC_MSG_CAN_INFO         0x04    ///< Host <-> Device. Query / Send CAN information.
#define SC_MSG_FILTER_INFO      0x05    ///< Host <-> Device. Query / Send CAN message filter information.

#define SC_MSG_NM_BITTIMING     0x10    ///< Host <-> Device. Configures nominal bittimings. Device responds with SC_MSG_ERROR
#define SC_MSG_DT_BITTIMING     0x11    ///< Host <-> Device. Configures data bittimings. Device responds with SC_MSG_ERROR

#define SC_MSG_FEATURES         0x13    ///< Host <-> Device. Sets supported device features. Device responds with SC_MSG_ERROR
#define SC_MSG_BUS              0x1e    ///< Host <-> Device. Go on / off bus. Device responds with SC_MSG_ERROR
#define SC_MSG_ERROR            0x1f    ///< Device -> Host. Error code of last command.

#define SC_MSG_CAN_LEN_MULTIPLE 4


#define SC_MSG_CAN_STATUS       0x20    ///< Device -> Host. Status of the CAN bus.
#define SC_MSG_CAN_RX           0x21    ///< Device -> Host. Received CAN frame.
#define SC_MSG_CAN_TX           0x22    ///< Host -> Device. Send CAN frame.
#define SC_MSG_CAN_TXR          0x23    ///< Device -> Host. CAN frame transmission receipt.
#define SC_MSG_CAN_ERROR        0x24    ///< Device -> Host. CAN frame error.
#define SC_MSG_CAN_SEQ          0x25    ///< Device -> Host. CAN sequence number.
#define SC_MSG_CAN_NOP          0x26    ///< Device -> Host. Used to fill URB to endpoint transfer size


#define SC_MSG_USER_OFFSET      0x80    ///< Custom device messages

#define SC_BYTE_ORDER_LE        0
#define SC_BYTE_ORDER_BE        1

#define SC_FEATURE_FLAG_FDF             0x0001 ///< Device supports CAN-FD standard.
#define SC_FEATURE_FLAG_EHD             0x0002 ///< Device supports disabling protocol exception handling in CAN-FD mode. When disabled, a CAN error frame will be transmitted (during normal operation) instead of switching from receiver to integrating node state.
#define SC_FEATURE_FLAG_DAR             0x0004 ///< Device supports disabling of automatic re-transmissions. When disabled, a CAN frame that has lost arbitration or was interrupted by an bus error will not be re-transmitted.
#define SC_FEATURE_FLAG_FLT             0x0008 ///< Device supports rx message filters
#define SC_FEATURE_FLAG_TXR             0x0010 ///< Device supports CAN frame transmission receipts.
#define SC_FEATURE_FLAG_GEN             0x0020 ///< Device supports tx message generators
#define SC_FEATURE_FLAG_MON_MODE        0x0100 ///< Device supports monitoring mode.
#define SC_FEATURE_FLAG_RES_MODE        0x0200 ///< Device supports restricted mode.
#define SC_FEATURE_FLAG_EXT_LOOP_MODE   0x0400 ///< Device supports external loopback mode. Transmitted messges are treated as received messages.
#define SC_FEATURE_FLAG_USER_OFFSET     0x1000 ///< Custom feature flags


#define SC_CAN_FRAME_FLAG_EXT         0x01 ///< Extended (29 bit id) frame
#define SC_CAN_FRAME_FLAG_RTR         0x02 ///< Remote request frame
#define SC_CAN_FRAME_FLAG_FDF         0x04 ///< CAN-FD frame
#define SC_CAN_FRAME_FLAG_BRS         0x08 ///< CAN-FD bitrate switching (set zero to transmit at arbitration rate)
#define SC_CAN_FRAME_FLAG_ESI         0x10 ///< Set to 1 to transmit with active error state
#define SC_CAN_FRAME_FLAG_DRP         0x20 ///< CAN frame was dropped due to full tx fifo (only received if TXR feature active)

#define SC_CAN_STATUS_ERROR_ACTIVE          0x0
#define SC_CAN_STATUS_ERROR_WARNING         0x1
#define SC_CAN_STATUS_ERROR_PASSIVE         0x2
#define SC_CAN_STATUS_BUS_OFF               0x3

#define SC_CAN_ERROR_NONE       0x0 ///< No error occurred
#define SC_CAN_ERROR_STUFF      0x1 ///< Stuff Error: More than 5 equal bits in a sequence have occurred in a part of a received message where this is not allowed.
#define SC_CAN_ERROR_FORM       0x2 ///< Form Error: A fixed format part of a received frame has the wrong format.
#define SC_CAN_ERROR_ACK        0x3 ///< Ack Error: The message transmitted by the CAN was not acknowledged by another node.
#define SC_CAN_ERROR_BIT1       0x4 ///< Bit1 Error: During the transmission of a message (with the exception of the arbitration field), the device wanted to send a recessive level (bit of logical value ‘1’), but the monitored bus was dominant.
#define SC_CAN_ERROR_BIT0       0x5 ///< Bit0 Error: During the transmission of a message (or acknowledge bit, or active error flag, or overload flag), the device wanted to send a dominant level (data or identifier bit logical value ‘0’), but the monitored bus value was recessive. During Bus_Off recovery this status is set each time a sequence of 11 recessive bits have been monitored. This enables the CPU to monitor the proceeding of the Bus_Off recovery sequence (indicating the bus is not stuck at dominant or continuously disturbed).
#define SC_CAN_ERROR_CRC        0x6 ///< CRC Error: The CRC checksum of a received message was incorrect. The CRC of an incoming message does not match with the CRC calculated from the received data.

#define SC_CAN_ERROR_FLAG_RXTX_TX   0x1 ///< Error on transmit, if unset on receive.
#define SC_CAN_ERROR_FLAG_NMDT_DT   0x2 ///< Error during data, if unset during arbitration.


#define SC_CAN_STATE_SYNC       0x0 ///< Node is synchronizing on CAN communication.
#define SC_CAN_STATE_IDLE       0x1 ///< Node is neither receiver nor transmitter.
#define SC_CAN_STATE_RX         0x2 ///< Node is operating as receiver.
#define SC_CAN_STATE_TX         0x3 ///< Node is operating as transmitter.




#define SC_CAN_STATUS_FLAG_TXR_DESYNC       0x1 ///< no USB buffer space to queue TXR message
#define SC_CAN_STATUS_FLAG_IRQ_QUEUE_FULL   0x2 ///< no space in interrupt -> task queue



/**
 * Features manipulated with SC_MSG_FEATURE
 */
#define SC_FEAT_OP_CLEAR        0x00 ///< Clear optional features
#define SC_FEAT_OP_OR           0x01 ///< Or (add) features


/**
 * Command error codes
 */
#define SC_ERROR_UNKNOWN       -1 ///< Unknown error
#define SC_ERROR_NONE           0 ///< No error
#define SC_ERROR_SHORT          1 ///< Message too short
#define SC_ERROR_PARAM          2 ///< Invalid parameter in request
#define SC_ERROR_BUSY           3 ///< Request cannot be processed currently. This typically happens when the device is on bus.
#define SC_ERROR_UNSUPPORTED    4 ///< Feature not supported


struct sc_msg_header {
    uint8_t id;
    uint8_t len;
} SC_PACKED;

/**
 * This is the only message that uses non-device byte order.
 */
struct sc_msg_hello {
    uint8_t id;
    uint8_t len;
    uint8_t proto_version;
    uint8_t byte_order;
    uint16_t cmd_buffer_size; ///< always in network byte order
    uint16_t unused;
} SC_PACKED;

/**
 * Request from host to device.
 */
struct sc_msg_req {
    uint8_t id;
    uint8_t len;
    uint8_t unused[2];
} SC_PACKED;

/**
 * Response from device to host.
 */
struct sc_msg_error {
    uint8_t id;
    uint8_t len;
    uint8_t unused;
    int8_t error;
} SC_PACKED;

struct sc_msg_dev_info {
    uint8_t id;
    uint8_t len;
    uint16_t feat_perm;    ///< Features permanently enabled (cannot be be cleared with SC_MSG_FEATURES)
    uint16_t feat_conf;    ///< Features enabled through configuration (SC_MSG_FEATURES)
    uint8_t unused;
    uint8_t sn_len;
    uint8_t sn_bytes[16];
    uint8_t fw_ver_major;
    uint8_t fw_ver_minor;
    uint8_t fw_ver_patch;
    uint8_t name_len;
    uint8_t name_bytes[32];
} SC_PACKED;

struct sc_msg_can_info {
    uint8_t id;
    uint8_t len;
    uint16_t msg_buffer_size;
    uint32_t can_clk_hz;
    uint16_t nmbt_brp_max;
    uint16_t nmbt_tseg1_max;
    uint8_t nmbt_tseg1_min;
    uint8_t nmbt_brp_min;
    uint8_t nmbt_sjw_max;
    uint8_t nmbt_tseg2_min;
    uint8_t nmbt_tseg2_max;
    uint8_t dtbt_brp_max;
    uint8_t dtbt_brp_min;
    uint8_t dtbt_tseg1_min;
    uint8_t dtbt_tseg1_max;
    uint8_t dtbt_sjw_max;
    uint8_t dtbt_tseg2_min;
    uint8_t dtbt_tseg2_max;
    uint8_t tx_fifo_size;
    uint8_t rx_fifo_size;
    uint8_t unused[2];
} SC_PACKED;

struct sc_msg_filter_info {
    uint8_t id;
    uint8_t len;
    uint8_t unused0[2];
} SC_PACKED;

struct sc_msg_config {
    uint8_t id;
    uint8_t len;
    uint16_t arg;
} SC_PACKED;

struct sc_msg_features {
    uint8_t id;
    uint8_t len;
    uint8_t unused;
    uint8_t op;
    uint32_t arg;
} SC_PACKED;

struct sc_msg_bittiming {
    uint8_t id;
    uint8_t len;
    uint8_t sjw;
    uint8_t tseg2;
    uint16_t brp;
    uint16_t tseg1;
} SC_PACKED;

struct sc_msg_can_status {
    uint8_t id;
    uint8_t len;
    uint8_t flags;              ///< CAN bus status flags
    uint8_t bus_status;
    uint32_t timestamp_us;
    uint16_t rx_lost;           ///< messages CAN -> USB lost since last time due to full rx fifo
    uint16_t tx_dropped;        ///< messages USB -> CAN dropped since last time due of full tx fifo
    uint8_t rx_errors;          ///< CAN rx error counter
    uint8_t tx_errors;          ///< CAN tx error counter
    uint8_t rx_fifo_size;       ///< CAN rx fifo fill state
    uint8_t tx_fifo_size;       ///< CAN tx fifo fill state
} SC_PACKED;

struct sc_msg_can_error {
    uint8_t id;
    uint8_t len;
    uint8_t error;
    uint8_t flags;
    uint32_t timestamp_us;
} SC_PACKED;

struct sc_msg_can_rx {
    uint8_t id;
    uint8_t len;            ///< must be a multiple of 4
    uint8_t dlc;
    uint8_t flags;
    uint32_t can_id;
    uint32_t timestamp_us;
    uint8_t data[0];
} SC_PACKED;

struct sc_msg_can_tx {
    uint8_t id;
    uint8_t len;            ///< must be a multiple of 4
    uint8_t dlc;
    uint8_t flags;
    uint32_t can_id;
    uint8_t track_id;
    uint8_t data[0];
} SC_PACKED;

struct sc_msg_can_txr {
    uint8_t id;
    uint8_t len;
    uint8_t flags;
    uint8_t track_id;
    uint32_t timestamp_us;
} SC_PACKED;

struct sc_msg_can_seq {
    uint8_t id;
    uint8_t len;
    uint16_t seq;
} SC_PACKED;

enum {
    sc_static_assert_sizeof_sc_msg_header_is_2 = sizeof(int[sizeof(struct sc_msg_header)  == 2 ? 1 : -1]),
    sc_static_assert_sc_msg_req_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_req) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_error_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_error) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_hello_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_hello) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_dev_info_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_dev_info) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_bittiming_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_bittiming) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_config_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_config) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_can_info_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_can_info) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_features_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_features) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_can_txr_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_can_txr) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_can_status_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_can_status) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_can_error_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_can_error) & 0x3) == 0 ? 1 : -1]),
    sc_static_assert_sc_msg_can_seq_is_a_multiple_of_4 = sizeof(int[(sizeof(struct sc_msg_can_seq) & 0x3) == 0 ? 1 : -1]),
};

#ifdef __cplusplus
} // extern "C"
#endif
