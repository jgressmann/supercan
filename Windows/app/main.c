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

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "supercan_winapi.h"
#include "supercan_dll.h"
#include "supercan_misc.h"
#include "can_bit_timing.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>

#ifndef _countof
#   define _countof(x) (sizeof(x)/sizeof((x)[0]))
#endif

#define CMD_TIMEOUT_MS 1000

#define SC_DLL_ERROR_BUFFER_TOO_SMALL 1000
#define SC_DLL_ERROR_INSUFFICIENT_DATA 1001




static inline uint8_t dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return map[dlc & 0xf];
}

static bool is_false(char const* str)
{
    return  
        0 == _stricmp(str, "0") ||
        0 == _stricmp(str, "false") ||
        0 == _stricmp(str, "no") ||
        0 == _stricmp(str, "off");
}

static void usage(FILE* stream)
{
    fprintf(stream, "SuperCAN demo app (c) 2020 Jean Gressmann <jean@0x42.de>\n");
    fprintf(stream, "supercan_app [options]\n");
    fprintf(stream, "\n");
    fprintf(stream, "-h, --help, /?     print this help\n");
    fprintf(stream, "--nbitrate INT     nominal bitrate\n");
    fprintf(stream, "--dbitrate INT     data bitrate (CAN-FD)\n");
    fprintf(stream, "--nswj INT         nominal SJW (defaults to 1)\n");
    fprintf(stream, "--dswj INT         data SJW (defaults to 1)\n");
    fprintf(stream, "--fd BOOL          enable or disable CAN-FD format (defaults to off)\n");
    fprintf(stream, "--log ITEM         enables logging of ITEM which is one of\n");
    fprintf(stream, "   NONE:       no logging\n");
    fprintf(stream, "   RX_DT:      log rx message timestamp deltas\n");
    fprintf(stream, "   RX_MSG:     log rx message content\n");
    fprintf(stream, "   BUS_STATE:  log bus status information\n");
    fprintf(stream, "   TX_MSG:         log tx message information\n");
    fprintf(stream, "   TXR:        log tx message receipts\n");
    fprintf(stream, "   ALL:        log everything\n");
}

#define LOG_FLAG_RX_DT      0x00000001
#define LOG_FLAG_RX_MSG     0x00000002
#define LOG_FLAG_BUS_STATE  0x00000004
#define LOG_FLAG_TX_MSG     0x00000008
#define LOG_FLAG_TXR        0x00000010



struct can_state {
    sc_dev_t* dev;
    struct sc_dev_time_tracker tt;
    uint64_t rx_last_ts;
    unsigned log_flags;
    bool rx_has_xtd_frame;
    bool rx_has_fdf_frame;
};



static bool process_buffer(
    struct can_state* s,
    uint8_t *ptr, uint16_t size, uint16_t *left)
{
    // process buffer
    PUCHAR in_beg = ptr;
    PUCHAR in_end = in_beg + size;
    PUCHAR in_ptr = in_beg;

    *left = 0;

    while (in_ptr + SC_MSG_HEADER_LEN <= in_end) {
        struct sc_msg_header const* msg = (struct sc_msg_header const*)in_ptr;
        if (in_ptr + msg->len > in_end) {
            *left = (uint16_t)(in_end - in_ptr);
            break;
        }

        assert(msg->len);

        if (msg->len < SC_MSG_HEADER_LEN) {
            fprintf(stderr, "malformed msg, len < SC_MSG_HEADER_LEN\n");
            return false;
        }

        in_ptr += msg->len;

        switch (msg->id) {
        case SC_MSG_EOF: {
            in_ptr = in_end;
        } break;
        case SC_MSG_CAN_STATUS: {
            struct sc_msg_can_status const* status = (struct sc_msg_can_status const*)msg;
            if (msg->len < sizeof(*status)) {
                fprintf(stderr, "malformed sc_msg_status\n");
                return false;
            }

            uint32_t timestamp_us = s->dev->dev_to_host32(status->timestamp_us);
            uint16_t rx_lost = s->dev->dev_to_host16(status->rx_lost);
            uint16_t tx_dropped = s->dev->dev_to_host16(status->tx_dropped);

            sc_tt_track(&s->tt, timestamp_us);

            if (s->log_flags & LOG_FLAG_BUS_STATE) {
                fprintf(stdout, "rx lost=%u tx dropped=%u rx errors=%u tx errors=%u bus=", rx_lost, tx_dropped, status->rx_errors, status->tx_errors);
                switch (status->bus_status) {
                case SC_CAN_STATUS_ERROR_ACTIVE:
                    fprintf(stdout, "error_active");
                    break;
                case SC_CAN_STATUS_ERROR_WARNING:
                    fprintf(stdout, "error_warning");
                    break;
                case SC_CAN_STATUS_ERROR_PASSIVE:
                    fprintf(stdout, "error_passive");
                    break;
                case SC_CAN_STATUS_BUS_OFF:
                    fprintf(stdout, "off");
                    break;
                default:
                    fprintf(stdout, "unknown");
                    break;
                }
                fprintf(stdout, "\n");
            }
        } break;
        case SC_MSG_CAN_ERROR: {
            struct sc_msg_can_error const* error_msg = (struct sc_msg_can_error const*)msg;
            if (msg->len < sizeof(*error_msg)) {
                fprintf(stderr, "malformed sc_msg_can_error\n");
                return false;
            }

            uint32_t timestamp_us = s->dev->dev_to_host32(error_msg->timestamp_us);

            sc_tt_track(&s->tt, timestamp_us);

            if (SC_CAN_ERROR_NONE != error_msg->error) {
                fprintf(
                    stdout, "%s %s ",
                    (error_msg->flags & SC_CAN_ERROR_FLAG_RXTX_TX) ? "tx" : "rx",
                    (error_msg->flags & SC_CAN_ERROR_FLAG_NMDT_DT) ? "data" : "arbitration");
                switch (error_msg->error) {
                case SC_CAN_ERROR_STUFF:
                    fprintf(stdout, "stuff ");
                    break;
                case SC_CAN_ERROR_FORM:
                    fprintf(stdout, "form ");
                    break;
                case SC_CAN_ERROR_ACK:
                    fprintf(stdout, "ack ");
                    break;
                case SC_CAN_ERROR_BIT1:
                    fprintf(stdout, "bit1 ");
                    break;
                case SC_CAN_ERROR_BIT0:
                    fprintf(stdout, "bit0 ");
                    break;
                case SC_CAN_ERROR_CRC:
                    fprintf(stdout, "crc ");
                    break;
                default:
                    fprintf(stdout, "<unknown> ");
                    break;
                }
                fprintf(stdout, "error\n");
            }
        } break;
        case SC_MSG_CAN_RX: {
            struct sc_msg_can_rx const* rx = (struct sc_msg_can_rx const*)msg;
            uint32_t can_id = s->dev->dev_to_host32(rx->can_id);
            uint32_t timestamp_us = s->dev->dev_to_host32(rx->timestamp_us);
            uint8_t len = dlc_to_len(rx->dlc);
            uint8_t bytes = sizeof(*rx);

            uint64_t ts_us = sc_tt_track(&s->tt, timestamp_us);

            if (!(rx->flags & SC_CAN_FRAME_FLAG_RTR)) {
                bytes += len;
            }

            if (msg->len < len) {
                fprintf(stderr, "malformed sc_msg_can_rx\n");
                return false;
            }

            if (s->log_flags & LOG_FLAG_RX_DT) {
                int64_t dt_us = 0;
                if (s->rx_last_ts) {
                    dt_us = ts_us - s->rx_last_ts;
                    if (dt_us < 0) {
                        fprintf(stderr, "WARN negative rx msg dt [us]: %lld\n", dt_us);
                    }
                }
                else {
                    s->rx_last_ts = ts_us;
                }

                fprintf(stdout, "rx delta %.3f [ms]\n", dt_us * 1e-3f);
            }

            if (s->log_flags & LOG_FLAG_RX_MSG) {
                if (rx->flags & SC_CAN_FRAME_FLAG_EXT) {
                    s->rx_has_xtd_frame = true;
                }

                if (rx->flags & SC_CAN_FRAME_FLAG_FDF) {
                    s->rx_has_fdf_frame = true;
                }

                if (s->rx_has_xtd_frame) {
                    fprintf(stdout, "%8X ", can_id);
                }
                else {
                    fprintf(stdout, "%3X ", can_id);
                }

                if (s->rx_has_fdf_frame) {
                    fprintf(stdout, "[%02u] ", len);
                }
                else {
                    fprintf(stdout, "[%u] ", len);
                }

                if (rx->flags & SC_CAN_FRAME_FLAG_RTR) {
                    fprintf(stdout, "RTR");
                }
                else {
                    for (uint8_t i = 0; i < len; ++i) {
                        fprintf(stdout, "%02X ", rx->data[i]);
                    }
                }
                fputc('\n', stdout);
            }
        } break;
        case SC_MSG_CAN_TXR: {
            struct sc_msg_can_txr const* txr = (struct sc_msg_can_txr const*)msg;
            if (msg->len < sizeof(*txr)) {
                fprintf(stderr, "malformed sc_msg_can_txr\n");
                return false;
            }

            uint32_t timestamp_us = s->dev->dev_to_host32(txr->timestamp_us);

            sc_tt_track(&s->tt, timestamp_us);

            if (s->log_flags & LOG_FLAG_TXR) {
                if (txr->flags & SC_CAN_FRAME_FLAG_DRP) {
                    fprintf(stdout, "tracked message %#02x was dropped @ %08x\n", txr->track_id, timestamp_us);
                }
                else {
                    fprintf(stdout, "tracked message %#02x was sent @ %08x\n", txr->track_id, timestamp_us);
                }
            }
        } break;
        default:
            break;
        }
    }

    return true;
}

static int process_can(
    void* ctx,
    const uint8_t* ptr, uint16_t size)
{
    uint16_t left = 0;
    if (!process_buffer(ctx, (uint8_t * )ptr, size, &left)) {
        return -1;
    }

    if (left) {
        return -1;
    }

    return 0;
}

int main(int argc, char** argv)
{
    struct can_state can_state;
    int error = SC_DLL_ERROR_NONE;
    uint32_t count = 0;
    sc_dev_t* dev = NULL;
    sc_cmd_ctx_t cmd_ctx;
    sc_can_stream_t stream = NULL;
    bool fdf = false;
    unsigned log_flags = 0;
    struct can_bit_timing_settings nominal_settings, data_settings;
    struct can_bit_timing_hw_contraints nominal_hw_constraints, data_hw_constraints;
    struct can_bit_timing_constraints_real nominal_user_constraints, data_user_constraints;
    nominal_user_constraints.bitrate = 500000;
    nominal_user_constraints.min_tqs = 0;
    nominal_user_constraints.sample_point = 0.75f;
    nominal_user_constraints.sjw = 1;
    data_user_constraints.bitrate = 500000;
    data_user_constraints.min_tqs = 0;
    data_user_constraints.sample_point = 0.75f;
    data_user_constraints.sjw = 1;
    
    PUCHAR msg_tx_buffer = NULL;
    struct sc_msg_dev_info dev_info;
    struct sc_msg_can_info can_info;
    DWORD transferred = 0;
    char serial_str[1 + sizeof(dev_info.sn_bytes) * 2] = { 0 };
    char name_str[1 + sizeof(dev_info.name_bytes)] = { 0 };
    uint8_t track_id = 0;
    uint32_t tx_counter = 0;

    memset(&cmd_ctx, 0, sizeof(cmd_ctx));
    memset(&can_state, 0, sizeof(can_state));
    
    sc_tt_init(&can_state.tt);

    sc_init();

    for (int i = 1; i < argc; ) {
        if (0 == strcmp("-h", argv[i]) ||
            0 == strcmp("--help", argv[i]) ||
            0 == strcmp("/?", argv[i])) {
            usage(stdout);
            goto Exit;
        }
        else if (0 == strcmp("--fd", argv[i])) {
            if (i + 1 < argc) {
                fdf = !is_false(argv[i + 1]);
                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a boolean argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--log", argv[i])) {
            if (i + 1 < argc) {
                const char* arg = argv[i + 1];
                if (0 == _stricmp("RX_DT", arg)) {
                    log_flags |= LOG_FLAG_RX_DT;
                }
                else if (0 == _stricmp("RX_MSG", arg)) {
                    log_flags |= LOG_FLAG_RX_MSG;
                }
                else if (0 == _stricmp("BUS_STATE", arg)) {
                    log_flags |= LOG_FLAG_BUS_STATE;
                }
                else if (0 == _stricmp("TX_MSG", arg)) {
                    log_flags |= LOG_FLAG_TX_MSG;
                }
                else if (0 == _stricmp("TXR", arg)) {
                    log_flags |= LOG_FLAG_TXR;
                }
                else if (0 == _stricmp("NONE", arg)) {
                    log_flags = 0u;
                }
                else if (0 == _stricmp("ALL", arg)) {
                    log_flags = ~0u;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a boolean argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--nbitrate", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                nominal_user_constraints.bitrate = (uint32_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (nominal_user_constraints.bitrate == 0 || nominal_user_constraints.bitrate > UINT32_C(1000000)) {
                    fprintf(stderr, "ERROR invalid bitrate %lu\n", (unsigned long)nominal_user_constraints.bitrate);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--dbitrate", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                data_user_constraints.bitrate = (uint32_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (data_user_constraints.bitrate == 0 || data_user_constraints.bitrate > UINT32_C(8000000)) {
                    fprintf(stderr, "ERROR invalid bitrate %lu\n", (unsigned long)data_user_constraints.bitrate);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--nsjw", argv[i])) {
            if (i + 1 < argc) {
                char* end = NULL;
                nominal_user_constraints.sjw = (uint8_t)strtoul(argv[i + 1], &end, 10);
                if (!end || end == argv[i + 1]) {
                    fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                if (nominal_user_constraints.sjw == 0) {
                    fprintf(stderr, "ERROR invalid sjw %u\n", (unsigned)nominal_user_constraints.sjw);
                    error = SC_DLL_ERROR_INVALID_PARAM;
                    goto Exit;
                }

                i += 2;
            }
            else {
                fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }
        }
        else if (0 == strcmp("--dsjw", argv[i])) {
        if (i + 1 < argc) {
            char* end = NULL;
            data_user_constraints.sjw = (uint8_t)strtoul(argv[i + 1], &end, 10);
            if (!end || end == argv[i + 1]) {
                fprintf(stderr, "ERROR failed to convert '%s' to integer\n", argv[i + 1]);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }

            if (data_user_constraints.sjw == 0) {
                fprintf(stderr, "ERROR invalid sjw %u\n", (unsigned)data_user_constraints.sjw);
                error = SC_DLL_ERROR_INVALID_PARAM;
                goto Exit;
            }

            i += 2;
        }
        else {
            fprintf(stderr, "ERROR %s expects a positive integer argument\n", argv[i]);
            error = SC_DLL_ERROR_INVALID_PARAM;
            goto Exit;
        }
        }
        else {
            ++i;
        }
    }

    can_state.log_flags = log_flags;


    error = sc_dev_scan();
    if (error) {
        fprintf(stderr, "sc_dev_scan failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }


    error = sc_dev_count(&count);
    if (error) {
        fprintf(stderr, "sc_dev_count failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    if (!count) {
        fprintf(stdout, "no " SC_NAME " devices found\n");
        goto Exit;
    }

    fprintf(stdout, "%u " SC_NAME " devices found\n", count);

    error = sc_dev_open(0, &dev);
    if (error) {
        fprintf(stderr, "sc_dev_open failed: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    can_state.dev = dev;

    fprintf(stdout, "cmd epp %#02x, can epp %#02x\n", dev->cmd_epp, dev->can_epp);
    error = sc_cmd_ctx_init(&cmd_ctx, dev);
    if (error) {
        goto Exit;
    }

    // fetch device info
    {
        struct sc_msg_req* req = (struct sc_msg_req*)cmd_ctx.tx_buffer;
        memset(req, 0, sizeof(*req));
        req->id = SC_MSG_DEVICE_INFO;
        req->len = sizeof(*req);
        uint16_t rep_len;
        error = sc_cmd_ctx_run(&cmd_ctx, req->len, &rep_len, CMD_TIMEOUT_MS);
        if (error) {
            goto Exit;
        }

        if (rep_len < sizeof(dev_info)) {
            fprintf(stderr, "failed to get device info\n");
            error = -1;
            goto Exit;
        }

        memcpy(&dev_info, cmd_ctx.rx_buffer, sizeof(dev_info));

        dev_info.feat_perm = dev->dev_to_host16(dev_info.feat_perm);
        dev_info.feat_conf = dev->dev_to_host16(dev_info.feat_conf);

        fprintf(stdout, "device features perm=%#04x conf=%#04x\n", dev_info.feat_perm, dev_info.feat_conf);

        for (size_t i = 0; i < min((size_t)dev_info.sn_len, _countof(serial_str) - 1); ++i) {
            snprintf(&serial_str[i * 2], 3, "%02x", dev_info.sn_bytes[i]);
        }

        dev_info.name_len = min((size_t)dev_info.name_len, sizeof(name_str) - 1);
        memcpy(name_str, dev_info.name_bytes, dev_info.name_len);
        name_str[dev_info.name_len] = 0;

        fprintf(stdout, "device identifies as %s, serial no %s, firmware version %u.% u.% u\n",
            name_str, serial_str, dev_info.fw_ver_major, dev_info.fw_ver_minor, dev_info.fw_ver_patch);

    }

    // fetch can info
    {
        struct sc_msg_req* req = (struct sc_msg_req*)cmd_ctx.tx_buffer;
        memset(req, 0, sizeof(*req));
        req->id = SC_MSG_CAN_INFO;
        req->len = sizeof(*req);
        uint16_t rep_len;
        error = sc_cmd_ctx_run(&cmd_ctx, req->len, &rep_len, CMD_TIMEOUT_MS);
        if (error) {
            goto Exit;
        }

        if (rep_len < sizeof(can_info)) {
            fprintf(stderr, "failed to get can info\n");
            error = -1;
            goto Exit;
        }

        memcpy(&can_info, cmd_ctx.rx_buffer, sizeof(can_info));

        can_info.can_clk_hz = dev->dev_to_host32(can_info.can_clk_hz);
        can_info.msg_buffer_size = dev->dev_to_host16(can_info.msg_buffer_size);
        can_info.nmbt_brp_max = dev->dev_to_host16(can_info.nmbt_brp_max);
        can_info.nmbt_tseg1_max = dev->dev_to_host16(can_info.nmbt_tseg1_max);

        
    }

    // compute hw settings
    {
        nominal_hw_constraints.brp_min = can_info.nmbt_brp_min;
        nominal_hw_constraints.brp_max = can_info.nmbt_brp_max;
        nominal_hw_constraints.brp_step = 1;
        nominal_hw_constraints.clock_hz = can_info.can_clk_hz;
        nominal_hw_constraints.sjw_max = can_info.nmbt_sjw_max;
        nominal_hw_constraints.tseg1_min = can_info.nmbt_tseg1_min;
        nominal_hw_constraints.tseg1_max = can_info.nmbt_tseg1_max;
        nominal_hw_constraints.tseg2_min = can_info.nmbt_tseg2_min;
        nominal_hw_constraints.tseg2_max = can_info.nmbt_tseg2_max;

        data_hw_constraints.brp_min = can_info.dtbt_brp_min;
        data_hw_constraints.brp_max = can_info.dtbt_brp_max;
        data_hw_constraints.brp_step = 1;
        data_hw_constraints.clock_hz = can_info.can_clk_hz;
        data_hw_constraints.sjw_max = can_info.dtbt_sjw_max;
        data_hw_constraints.tseg1_min = can_info.dtbt_tseg1_min;
        data_hw_constraints.tseg1_max = can_info.dtbt_tseg1_max;
        data_hw_constraints.tseg2_min = can_info.dtbt_tseg2_min;
        data_hw_constraints.tseg2_max = can_info.dtbt_tseg2_max;

        error = cbt_real(&nominal_hw_constraints, &nominal_user_constraints, &nominal_settings);
        switch (error) {
        case CAN_BTRE_NO_SOLUTION:
            fprintf(stderr, "The chosen nominal bitrate/sjw cannot be configured on the device.\n");
            error = -1;
            goto Exit;
        case CAN_BTRE_NONE:
            break;
        default:
            fprintf(stderr, "Ooops.\n");
            error = -1;
            goto Exit;
        }

        if (fdf) {
            error = cbt_real(&data_hw_constraints, &data_user_constraints, &data_settings);
            switch (error) {
            case CAN_BTRE_NO_SOLUTION:
                fprintf(stderr, "The chosen data bitrate/sjw cannot be configured on the device.\n");
                error = -1;
                goto Exit;
            case CAN_BTRE_NONE:
                break;
            default:
                fprintf(stderr, "Ooops.\n");
                error = -1;
                goto Exit;
            }
        }
    }
    
    msg_tx_buffer = malloc(256); // TX BUFFER WARNING
    if (!msg_tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }

    // setup device
    {
        unsigned cmd_count = 0;
        PUCHAR cmd_tx_ptr = cmd_ctx.tx_buffer;

        // clear features
        struct sc_msg_features* feat = (struct sc_msg_features*)cmd_tx_ptr;
        memset(feat, 0, sizeof(*feat));
        feat->id = SC_MSG_FEATURES;
        feat->len = sizeof(*feat);
        feat->op = SC_FEAT_OP_CLEAR;
        cmd_tx_ptr += feat->len;
        ++cmd_count;

        feat = (struct sc_msg_features*)cmd_tx_ptr;
        memset(feat, 0, sizeof(*feat));
        feat->id = SC_MSG_FEATURES;
        feat->len = sizeof(*feat);
        feat->op = SC_FEAT_OP_OR;
        // try to enable CAN-FD and TXR
        feat->arg = (dev_info.feat_perm | dev_info.feat_conf) & 
            ((fdf ? SC_FEATURE_FLAG_FDF : 0) | SC_FEATURE_FLAG_TXR);
        cmd_tx_ptr += feat->len;
        ++cmd_count;

        
        // set nominal bittiming
        struct sc_msg_bittiming* bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
        memset(bt, 0, sizeof(*bt));
        bt->id = SC_MSG_NM_BITTIMING;
        bt->len = sizeof(*bt);
        bt->brp = dev->dev_to_host16(nominal_settings.brp);
        bt->sjw = nominal_settings.sjw;
        bt->tseg1 = dev->dev_to_host16(nominal_settings.tseg1);
        bt->tseg2 = nominal_settings.tseg2;
        cmd_tx_ptr += bt->len;
        ++cmd_count;

        if (fdf && ((dev_info.feat_perm | dev_info.feat_conf) & SC_FEATURE_FLAG_FDF)) {
            // CAN-FD capable & configured -> set data bitrate

            bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
            memset(bt, 0, sizeof(*bt));
            bt->id = SC_MSG_DT_BITTIMING;
            bt->len = sizeof(*bt);
            bt->brp = dev->dev_to_host16(data_settings.brp);
            bt->sjw = data_settings.sjw;
            bt->tseg1 = dev->dev_to_host16(data_settings.tseg1);
            bt->tseg2 = data_settings.tseg2;
            cmd_tx_ptr += bt->len;
            ++cmd_count;
        }        

        struct sc_msg_config* bus_on = (struct sc_msg_config*)cmd_tx_ptr;
        memset(bus_on, 0, sizeof(*bus_on));
        bus_on->id = SC_MSG_BUS;
        bus_on->len = sizeof(*bus_on);
        bus_on->arg = dev->dev_to_host16(1);
        cmd_tx_ptr += bus_on->len;
        ++cmd_count;

        uint16_t rep_len;
        error = sc_cmd_ctx_run(&cmd_ctx, (uint16_t)(cmd_tx_ptr - cmd_ctx.tx_buffer), &rep_len, CMD_TIMEOUT_MS);
        if (error) {
            goto Exit;
        }


        if (rep_len < cmd_count * sizeof(struct sc_msg_error)) {
            fprintf(stderr, "failed to setup device\n");
            error = -1;
            goto Exit;
        }

        struct sc_msg_error* error_msgs = (struct sc_msg_error*)cmd_ctx.rx_buffer;
        for (unsigned i = 0; i < cmd_count; ++i) {            
            struct sc_msg_error* error_msg = &error_msgs[i];
            if (SC_ERROR_NONE != error_msg->error) {
                fprintf(stderr, "cmd index %u failed: %d\n", i, error_msg->error);
                error = -1;
                goto Exit;
            }
        }
    }

    error = sc_can_stream_init(dev, can_info.msg_buffer_size, &can_state, process_can, -1, &stream);
    if (error) {
        goto Exit;
    }

    const DWORD WAIT_TIMEOUT_MS = 500; // tx on timeout
    DWORD last_send = 0;
    while (1) {
        error = sc_can_stream_rx(stream, 100);
        if (error) {
            if (SC_DLL_ERROR_TIMEOUT != error) {                
                fprintf(stderr, "sc_can_stream_run failed: %s (%d)\n", sc_strerror(error), error);
                break;
            }
        }

        DWORD now = GetTickCount();
        if (now - last_send >= WAIT_TIMEOUT_MS) {
            uint16_t bytes = 0;
            PUCHAR ptr = msg_tx_buffer;
            struct sc_msg_can_tx* tx = (struct sc_msg_can_tx*)ptr;
            memset(tx, 0, 128);

            bytes = sizeof(*tx) + 64;
            if (bytes & 3) {
                bytes += 4 - (bytes & 3);
            }

            ptr += bytes;

            tx->id = SC_MSG_CAN_TX;
            tx->len = (uint8_t)bytes;
            tx->can_id = 0x42;
            tx->dlc = 15;
            tx->flags = SC_CAN_FRAME_FLAG_FDF | SC_CAN_FRAME_FLAG_BRS;
            tx->track_id = track_id++;
            tx->data[0] = 0xde;
            tx->data[1] = 0xad;
            tx->data[2] = 0xbe;
            tx->data[3] = 0xef;
            tx->data[4] = (uint8_t)(tx_counter >> 24);
            tx->data[5] = (uint8_t)(tx_counter >> 16);
            tx->data[6] = (uint8_t)(tx_counter >> 8);
            tx->data[7] = (uint8_t)(tx_counter >> 0);
            

            tx = (struct sc_msg_can_tx*)ptr;
            bytes = sizeof(*tx);
            if (bytes & 3) {
                bytes += 4 - (bytes & 3);
            }

            ptr += bytes;

            tx->id = SC_MSG_CAN_TX;
            tx->len = (uint8_t)bytes;
            tx->can_id = 0x12345;
            tx->dlc = 0;
            tx->flags = SC_CAN_FRAME_FLAG_EXT;
            tx->track_id = track_id++;
            
            last_send = now;

            bytes = (uint16_t)(ptr - msg_tx_buffer);
            size_t w = 0;
            error = sc_can_stream_tx(stream, msg_tx_buffer, bytes, -1, &w);
            if (error) {
                if (SC_DLL_ERROR_AGAIN != error) {
                    fprintf(stderr, "sc_can_stream_tx failed: %s (%d)\n", sc_strerror(error), error);
                    goto Exit;
                }
            }
            else {
                if (w == bytes) {
                    fprintf(stderr, "tx %lx\n", tx_counter);
                    ++tx_counter;
                }
            }
        }
    }


Exit:
    sc_can_stream_uninit(stream);

    if (dev) {
        sc_cmd_ctx_uninit(&cmd_ctx);
        sc_dev_close(dev);
    }

    free(msg_tx_buffer);
    
    sc_uninit();
    return error;
}