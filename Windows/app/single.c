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


#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define CMD_TIMEOUT_MS 1000

struct can_state {
    sc_dev_t* dev;
    uint64_t rx_last_ts;
    bool rx_has_xtd_frame;
    bool rx_has_fdf_frame;
};


static bool process_buffer(
    struct app_ctx* ac,
    uint8_t* ptr, uint16_t size, uint16_t* left)
{
    // process buffer
    struct can_state* s = ac->priv;
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

            sc_tt_track(&ac->tt, timestamp_us);

            if (ac->log_flags & LOG_FLAG_BUS_STATE) {
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

            sc_tt_track(&ac->tt, timestamp_us);

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

            uint64_t ts_us = sc_tt_track(&ac->tt, timestamp_us);

            if (!(rx->flags & SC_CAN_FRAME_FLAG_RTR)) {
                bytes += len;
            }

            if (msg->len < len) {
                fprintf(stderr, "malformed sc_msg_can_rx\n");
                return false;
            }

            if (ac->log_flags & LOG_FLAG_RX_DT) {
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

            if (ac->log_flags & LOG_FLAG_RX_MSG) {
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

            sc_tt_track(&ac->tt, timestamp_us);

            if (ac->log_flags & LOG_FLAG_TXR) {
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
    if (!process_buffer(ctx, (uint8_t*)ptr, size, &left)) {
        return -1;
    }

    if (left) {
        return -1;
    }

    return 0;
}

int run_single(struct app_ctx* ac)
{
    int error = SC_DLL_ERROR_NONE;
    struct can_state can_state;
    uint32_t count = 0;
    sc_dev_t* dev = NULL;
    sc_cmd_ctx_t cmd_ctx;
    sc_can_stream_t* stream = NULL;
    struct can_bit_timing_settings nominal_settings, data_settings;
    struct can_bit_timing_hw_contraints nominal_hw_constraints, data_hw_constraints;

    
    PUCHAR msg_tx_buffer = NULL;
    struct sc_msg_dev_info dev_info;
    struct sc_msg_can_info can_info;
    DWORD transferred = 0;
    char serial_str[1 + sizeof(dev_info.sn_bytes) * 2] = { 0 };
    char name_str[1 + sizeof(dev_info.name_bytes)] = { 0 };
    uint8_t track_id = 0;

    memset(&can_state, 0, sizeof(can_state));
    memset(&cmd_ctx, 0, sizeof(cmd_ctx));

    ac->priv = &can_state;
    
    sc_init();

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

    error = sc_dev_open_by_index(0, &dev);
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

        error = cbt_real(&nominal_hw_constraints, &ac->nominal_user_constraints, &nominal_settings);
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

        if (ac->fdf) {
            error = cbt_real(&data_hw_constraints, &ac->data_user_constraints, &data_settings);
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
            ((ac->fdf ? SC_FEATURE_FLAG_FDF : 0) | SC_FEATURE_FLAG_TXR);
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

        if (ac->fdf && ((dev_info.feat_perm | dev_info.feat_conf) & SC_FEATURE_FLAG_FDF)) {
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

    error = sc_can_stream_init(dev, can_info.msg_buffer_size, ac, process_can, -1, &stream);
    if (error) {
        goto Exit;
    }

    stream->user_handle = ac->shutdown_event;

    DWORD timeout_ms = 0;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (1) {
        error = sc_can_stream_rx(stream, timeout_ms);
        if (error) {
            if (SC_DLL_ERROR_USER_HANDLE_SIGNALED) {
                break;
            }

            if (SC_DLL_ERROR_TIMEOUT != error) {
                fprintf(stderr, "sc_can_stream_run failed: %s (%d)\n", sc_strerror(error), error);
                break;
            }
        }

        if (ac->tx_job_count) {
            timeout_ms = 0xffffffff;
            ULONGLONG now = GetTickCount64();

            for (size_t i = 0; i < ac->tx_job_count; ++i) {
                struct tx_job* job = &ac->tx_jobs[i];
                if (0 == job->last_tx_ts_ms ||
                    (job->interval_ms >= 0 && now - job->last_tx_ts_ms >= (unsigned)job->interval_ms)) {
                    job->last_tx_ts_ms = now;

                    struct sc_msg_can_tx* tx = (struct sc_msg_can_tx*)msg_tx_buffer;
                    uint8_t bytes = sizeof(*tx);
                    if (job->flags & SC_CAN_FRAME_FLAG_RTR) {

                    }
                    else {
                        bytes += dlc_to_len(job->dlc);
                        memcpy(tx->data, job->data, dlc_to_len(job->dlc));
                    }

                    if (bytes & (SC_MSG_CAN_LEN_MULTIPLE - 1)) {
                        bytes += SC_MSG_CAN_LEN_MULTIPLE - (bytes & (SC_MSG_CAN_LEN_MULTIPLE - 1));
                    }

                    tx->id = SC_MSG_CAN_TX;
                    tx->len = bytes;
                    tx->can_id = dev->dev_to_host32(job->can_id);
                    tx->dlc = job->dlc;
                    tx->flags = job->flags;
                    tx->track_id = track_id++;


                    error = sc_can_stream_tx(stream, msg_tx_buffer, bytes);
                    if (error) {
                        if (SC_DLL_ERROR_AGAIN != error) {
                            fprintf(stderr, "sc_can_stream_tx failed: %s (%d)\n", sc_strerror(error), error);
                            goto Exit;
                        }
                    }

                    if (job->interval_ms >= 0 && (DWORD)job->interval_ms < timeout_ms) {
                        timeout_ms = job->interval_ms;
                    }
                }
            }
        }
        else {
            timeout_ms = INFINITE;
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