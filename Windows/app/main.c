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
#include "sc_app.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef _countof
#   define _countof(x) (sizeof(x)/sizeof((x)[0]))
#endif

#define MAX_PENDING_READS 16
#define SC_DLL_ERROR_BUFFER_TOO_SMALL 1000
#define SC_DLL_ERROR_INSUFFICIENT_DATA 1001


static inline uint8_t dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return map[dlc & 0xf];
}


typedef struct sc_dev_context {
    sc_dev_t* dev;
    PUCHAR cmd_tx_buffer;
    PUCHAR cmd_rx_buffer;
    OVERLAPPED cmd_tx_ov, cmd_rx_ov;
} sc_dev_context_t;

static void sc_dev_ctx_uninit(sc_dev_context_t* ctx)
{
    if (ctx->cmd_tx_ov.hEvent) {
        CloseHandle(ctx->cmd_tx_ov.hEvent);
        ctx->cmd_tx_ov.hEvent = NULL;
    }

    if (ctx->cmd_rx_ov.hEvent) {
        CloseHandle(ctx->cmd_rx_ov.hEvent);
        ctx->cmd_rx_ov.hEvent = NULL;
    }

    if (ctx->cmd_tx_buffer) {
        free(ctx->cmd_tx_buffer);
        ctx->cmd_tx_buffer = NULL;
    }

    if (ctx->cmd_rx_buffer) {
        free(ctx->cmd_rx_buffer);
        ctx->cmd_rx_buffer = NULL;
    }
}

static int sc_dev_ctx_init(sc_dev_context_t* ctx, sc_dev_t* dev)
{
    int error = SC_DLL_ERROR_NONE;

    memset(ctx, 0, sizeof(*ctx));
    ctx->dev = dev;

    ctx->cmd_tx_ov.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ctx->cmd_tx_ov.hEvent) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }
    
    ctx->cmd_rx_ov.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ctx->cmd_rx_ov.hEvent) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    ctx->cmd_tx_buffer = malloc(ctx->dev->cmd_buffer_size);
    if (!ctx->cmd_tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }
    ctx->cmd_rx_buffer = malloc(ctx->dev->cmd_buffer_size);
    if (!ctx->cmd_rx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

Exit:
    return error;
Error:
    sc_dev_ctx_uninit(ctx);
    goto Exit;
}

static int sc_dev_ctx_send_receive_cmd(
    sc_dev_context_t* ctx, 
    size_t cmd_len, 
    size_t *response_len)
{
    int error = SC_DLL_ERROR_NONE;
    DWORD transferred = 0;
    sc_dev_t* dev = ctx->dev;


    if (cmd_len > dev->cmd_buffer_size) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    // submit in token
    while (1) {
        error = sc_dev_read(dev, dev->cmd_epp | 0x80, ctx->cmd_rx_buffer, dev->cmd_buffer_size, &ctx->cmd_rx_ov);
        if (SC_DLL_ERROR_NONE == error) {
            continue; // clear pending data
        }

        if (SC_DLL_ERROR_IO_PENDING != error) {
            return error;
        }

        break;
    }

    error = sc_dev_write(dev, dev->cmd_epp, ctx->cmd_tx_buffer, (ULONG)cmd_len, &ctx->cmd_tx_ov);
    switch (error) {
    case SC_DLL_ERROR_NONE:
    case SC_DLL_ERROR_IO_PENDING:
        break;
    default:
        return error;
    }
    

    error = sc_dev_result(dev, &transferred, &ctx->cmd_tx_ov, -1);
    if (error) {
        fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
        return error;
    }

    if (transferred != cmd_len) {
        fprintf(stderr, "sc_dev_write incomplete\n");
        return SC_DLL_ERROR_UNKNOWN;
    }

    error = sc_dev_result(dev, &transferred, &ctx->cmd_rx_ov, -1);
    if (error) {
        fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
        return error;
    }

    *response_len = transferred;

    return error;
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
    fprintf(stream, "--fd BOOL          enable or disable CAN-FD format\n");
    fprintf(stream, "--log ITEM         enables logging of ITEM which is one of\n");
    fprintf(stream, "   NONE:       no logging\n");
    fprintf(stream, "   RX_DT:      log rx message timestamp deltas\n");
    fprintf(stream, "   RX_MSG:     log rx message content\n");
    fprintf(stream, "   BUS_STATE:  log bus status information\n");
    fprintf(stream, "   TX:         log tx message information\n");
    fprintf(stream, "   TXR:        log tx message receipts\n");
    fprintf(stream, "   ALL:        log everything\n");
}

#define LOG_FLAG_RX_DT      0x00000001
#define LOG_FLAG_RX_MSG     0x00000002
#define LOG_FLAG_BUS_STATE  0x00000004
#define LOG_FLAG_TX         0x00000008
#define LOG_FLAG_TXR        0x00000010

int main(int argc, char** argv)
{
    struct sc_time_tracker tt;
    uint64_t rx_last_ts = 0;
    int error = SC_DLL_ERROR_NONE;
    uint32_t count;
    sc_dev_t* dev = NULL;
    sc_dev_context_t dev_ctx;
    bool fdf = false;
    unsigned log_flags = 0;
    
    PUCHAR msg_rx_buffers = NULL;
    PUCHAR msg_tx_buffer = NULL;
    HANDLE msg_read_events[MAX_PENDING_READS] = { 0 };
    OVERLAPPED msg_read_ovs[MAX_PENDING_READS] = { 0 };
    HANDLE msg_tx_event = NULL;
    OVERLAPPED msg_tx_ov;
    struct sc_msg_dev_info dev_info;
    struct sc_msg_can_info can_info;
    DWORD transferred = 0;
    char serial_str[1 + sizeof(dev_info.sn_bytes) * 2] = { 0 };
    char name_str[1 + sizeof(dev_info.name_bytes)] = { 0 };
    uint8_t track_id = 0;

    memset(&dev_ctx, 0, sizeof(dev_ctx));
    sc_tt_init(&tt);

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
                else if (0 == _stricmp("RX", arg)) {
                    log_flags |= LOG_FLAG_RX_MSG;
                }
                else if (0 == _stricmp("BUS_STATE", arg)) {
                    log_flags |= LOG_FLAG_BUS_STATE;
                }
                else if (0 == _stricmp("TX", arg)) {
                    log_flags |= LOG_FLAG_TX;
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
        else {
            ++i;
        }
    }

    for (size_t i = 0; i < _countof(msg_read_events); ++i) {
        HANDLE h = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!h) {
            error = -1;
            goto Exit;
        }

        msg_read_events[i] = h;
        msg_read_ovs[i].hEvent = h;
    }

    msg_tx_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!msg_tx_event) {
        error = -1;
        goto Exit;
    }

    msg_tx_ov.hEvent = msg_tx_event;

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

    fprintf(stdout, "cmd epp %#02x, can epp %#02x\n", dev->cmd_epp, dev->can_epp);
    error = sc_dev_ctx_init(&dev_ctx, dev);
    if (error) {
        goto Exit;
    }

    // fetch device info
    {
        struct sc_msg_req* req = (struct sc_msg_req*)dev_ctx.cmd_tx_buffer;
        memset(req, 0, sizeof(*req));
        req->id = SC_MSG_DEVICE_INFO;
        req->len = sizeof(*req);
        size_t rep_len;
        error = sc_dev_ctx_send_receive_cmd(&dev_ctx, req->len, &rep_len);
        if (error) {
            goto Exit;
        }

        if (rep_len < sizeof(dev_info)) {
            fprintf(stderr, "failed to get device info\n");
            error = -1;
            goto Exit;
        }

        memcpy(&dev_info, dev_ctx.cmd_rx_buffer, sizeof(dev_info));

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
        struct sc_msg_req* req = (struct sc_msg_req*)dev_ctx.cmd_tx_buffer;
        memset(req, 0, sizeof(*req));
        req->id = SC_MSG_CAN_INFO;
        req->len = sizeof(*req);
        size_t rep_len;
        error = sc_dev_ctx_send_receive_cmd(&dev_ctx, req->len, &rep_len);
        if (error) {
            goto Exit;
        }

        if (rep_len < sizeof(can_info)) {
            fprintf(stderr, "failed to get can info\n");
            error = -1;
            goto Exit;
        }

        memcpy(&can_info, dev_ctx.cmd_rx_buffer, sizeof(can_info));

        can_info.can_clk_hz = dev->dev_to_host32(can_info.can_clk_hz);
        can_info.msg_buffer_size = dev->dev_to_host16(can_info.msg_buffer_size);
        can_info.nmbt_brp_max = dev->dev_to_host16(can_info.nmbt_brp_max);
        can_info.nmbt_tseg1_max = dev->dev_to_host16(can_info.nmbt_tseg1_max);
    }

    // allocate buffers
    msg_rx_buffers = malloc(can_info.msg_buffer_size * _countof(msg_read_events));
    if (!msg_rx_buffers) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }
    
    msg_tx_buffer = malloc(can_info.msg_buffer_size);
    if (!msg_tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }


    // submit all reads tokens
    for (size_t i = 0; i < _countof(msg_read_events); ++i) {
        error = sc_dev_read(dev, dev->can_epp | 0x80, msg_rx_buffers + i * can_info.msg_buffer_size, can_info.msg_buffer_size, &msg_read_ovs[i]);
        if (SC_DLL_ERROR_IO_PENDING != error) {
            goto Exit;
        }
    }

    

    // setup device
    {
        unsigned cmd_count = 0;
        PUCHAR cmd_tx_ptr = dev_ctx.cmd_tx_buffer;

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
        // 500KBit@80MHz CAN clock
        // nominal brp=1 sjw=1 tseg1=139 tseg2=20 bitrate=500000 sp=874/1000
        bt->brp = dev->dev_to_host16(1);
        bt->sjw = 1;
        bt->tseg1 = dev->dev_to_host16(139);
        bt->tseg2 = 20;
        cmd_tx_ptr += bt->len;
        ++cmd_count;

        if ((dev_info.feat_perm | dev_info.feat_conf) & SC_FEATURE_FLAG_FDF) {
            // CAN-FD capable -> configure data bitrate

            bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
            memset(bt, 0, sizeof(*bt));
            bt->id = SC_MSG_DT_BITTIMING;
            bt->len = sizeof(*bt);
            // 2MBit@80MHz CAN clock
            // data brp = 1 sjw = 1 tseg1 = 29 tseg2 = 10 bitrate = 2000000 sp = 743 / 1000       
            bt->brp = 1;
            bt->sjw = 1;
            bt->tseg1 = 29;
            bt->tseg2 = 10;
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

        size_t rep_len;
        error = sc_dev_ctx_send_receive_cmd(&dev_ctx, cmd_tx_ptr - dev_ctx.cmd_tx_buffer, &rep_len);
        if (error) {
            goto Exit;
        }


        if (rep_len < cmd_count * sizeof(struct sc_msg_error)) {
            fprintf(stderr, "failed to setup device\n");
            error = -1;
            goto Exit;
        }

        struct sc_msg_error* error_msgs = (struct sc_msg_error*)dev_ctx.cmd_rx_buffer;
        for (unsigned i = 0; i < cmd_count; ++i) {            
            struct sc_msg_error* error_msg = &error_msgs[i];
            if (SC_ERROR_NONE != error_msg->error) {
                fprintf(stderr, "cmd index %u failed: %d\n", i, error_msg->error);
                error = -1;
                goto Exit;
            }
        }
    }

    const DWORD WAIT_TIMEOUT_MS = 500;
    DWORD last_send = GetTickCount();
    while (1) {
        DWORD result = WaitForMultipleObjects(_countof(msg_read_events), msg_read_events, FALSE, WAIT_TIMEOUT_MS);
        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + _countof(msg_read_events)) {
            size_t index = result - WAIT_OBJECT_0;
            error = sc_dev_result(dev, &transferred, &msg_read_ovs[index], 0);
            if (error) {
                fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }

            // process buffer
            PUCHAR in_beg = msg_rx_buffers + can_info.msg_buffer_size * index;
            PUCHAR in_end = in_beg + transferred;
            PUCHAR in_ptr = in_beg;
            while (in_ptr + SC_MSG_HEADER_LEN <= in_end) {
                struct sc_msg_header const* msg = (struct sc_msg_header const*)in_ptr;
                if (in_ptr + msg->len > in_end) {
                    fprintf(stderr, "malformed msg\n");
                    break;
                }

                if (!msg->len) {
                    break;
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
                        break;
                    }

                    uint32_t timestamp_us = dev->dev_to_host32(status->timestamp_us);
                    uint16_t rx_lost = dev->dev_to_host16(status->rx_lost);
                    uint16_t tx_dropped = dev->dev_to_host16(status->tx_dropped);

                    sc_track_ts(&tt, timestamp_us);

                    if (log_flags & LOG_FLAG_BUS_STATE) {
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
                        break;
                    }

                    uint32_t timestamp_us = dev->dev_to_host32(error_msg->timestamp_us);

                    sc_track_ts(&tt, timestamp_us);

                    if (SC_CAN_ERROR_NONE != error_msg->error) {
                        fprintf(
                            stdout, "%s %s ", 
                            (error_msg->flags & SC_CAN_ERROR_FLAG_RXTX_TX) ? "tx" : "rx",
                            (error_msg->flags& SC_CAN_ERROR_FLAG_NMDT_DT) ? "data" : "arbitration");
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
                    struct sc_msg_can_rx const *rx = (struct sc_msg_can_rx const*)msg;
                    uint32_t can_id = dev->dev_to_host32(rx->can_id);
                    uint32_t timestamp_us = dev->dev_to_host32(rx->timestamp_us);
                    uint8_t len = dlc_to_len(rx->dlc);
                    uint8_t bytes = sizeof(*rx);

                    uint64_t ts_us = sc_track_ts(&tt, timestamp_us);

                    if (!(rx->flags & SC_CAN_FRAME_FLAG_RTR)) {
                        bytes += len;
                    }

                    if (msg->len < len) {
                        fprintf(stderr, "malformed sc_msg_can_rx\n");
                        break;
                    }

                    if (log_flags & LOG_FLAG_RX_DT) {
                        int64_t dt_us = 0;
                        if (rx_last_ts) {
                            dt_us = ts_us - rx_last_ts;
                            if (dt_us < 0) {
                                fprintf(stderr, "WARN negative rx msg dt [us]: %lld\n", dt_us);
                            }
                        }
                        else {
                            rx_last_ts = ts_us;
                        }

                        fprintf(stdout, "rx delta %.3f [ms]\n", dt_us * 1e-3f);
                    }

                    if (log_flags & LOG_FLAG_RX_MSG) {
                        fprintf(stdout, "%x [%u] ", can_id, len);
                        if (rx->flags & SC_CAN_FRAME_FLAG_RTR) {
                            fprintf(stdout, "RTR");
                        }
                        else {
                            for (uint8_t i = 0; i < len; ++i) {
                                fprintf(stdout, "%02x ", rx->data[i]);
                            }
                        }
                        fputc('\n', stdout);
                    }
                } break;
                case SC_MSG_CAN_TXR: {
                    struct sc_msg_can_txr const* txr = (struct sc_msg_can_txr const*)msg;
                    uint32_t timestamp_us = dev->dev_to_host32(txr->timestamp_us);

                    sc_track_ts(&tt, timestamp_us);
                    
                    if (log_flags & LOG_FLAG_TXR) {
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

            // re-queue in token
            error = sc_dev_read(dev, dev->can_epp | 0x80, in_beg, can_info.msg_buffer_size, &msg_read_ovs[index]);
            if (error) {
                if (error != SC_DLL_ERROR_IO_PENDING) {
                    fprintf(stderr, "sc_dev_read failed: %s (%d)\n", sc_strerror(error), error);
                    goto Exit;
                }

                error = 0;
            }

            DWORD now = GetTickCount();
            if (now - last_send >= WAIT_TIMEOUT_MS) {
                goto send;
            }
        }
        else if (WAIT_TIMEOUT == result) {
            struct sc_msg_can_tx* tx;
send:
            tx = (struct sc_msg_can_tx*)msg_tx_buffer;
            tx->id = SC_MSG_CAN_TX;
            tx->len = sizeof(*tx) + 4;
            if (tx->len & 3) {
                tx->len += 4 - (tx->len & 3);
            }
            tx->can_id = 0x42;
            tx->dlc = 4;
            tx->flags = 0;
            tx->track_id = track_id++;
            tx->data[0] = 0xde;
            tx->data[1] = 0xad;
            tx->data[2] = 0xbe;
            tx->data[3] = 0xef;

            error = sc_dev_write(dev, dev->can_epp, (uint8_t*)tx, tx->len, &msg_tx_ov);
            if (error) {
                if (error != SC_DLL_ERROR_IO_PENDING) {
                    fprintf(stderr, "sc_dev_write failed: %s (%d)\n", sc_strerror(error), error);
                    goto Exit;
                }
            }

            error = sc_dev_result(dev, &transferred, &msg_tx_ov, -1);
            if (error) {
                fprintf(stderr, "sc_dev_result failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }

            last_send = GetTickCount();
        } else {
            fprintf(stderr, "WaitForMultipleObjects failed: %s (%d)\n", sc_strerror(error), error);
            break;
        }
    }

Exit:
    if (dev) {
        // cancel any pending I/O prior to event / buffer cleanup
        for (size_t i = 0; i < _countof(msg_read_events); ++i) {
            if (msg_read_ovs[i].hEvent) {
                sc_dev_cancel(dev, &msg_read_ovs[i]);
            }
        }

        if (msg_tx_ov.hEvent) {
            sc_dev_cancel(dev, &msg_tx_ov);
        }

        sc_dev_ctx_uninit(&dev_ctx);

        sc_dev_close(dev);
    }

    for (size_t i = 0; i < _countof(msg_read_events); ++i) {
        if (msg_read_events[i]) {
            CloseHandle(msg_read_events[i]);
        }
    }

    if (msg_tx_event) {
        CloseHandle(msg_tx_event);
    }

    free(msg_rx_buffers);
    free(msg_tx_buffer);
    
    sc_uninit();
    return error;
}