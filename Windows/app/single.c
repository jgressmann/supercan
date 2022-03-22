/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2022 Jean Gressmann <jean@0x42.de>
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
#include "supercan_misc.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define CMD_TIMEOUT_MS 1000


struct can_echo {
    uint32_t can_id;
    uint8_t dlc;
    uint8_t flags;
    uint8_t data[64];
};

struct can_state {
    sc_dev_t* dev;
    sc_can_stream_t* stream;
    struct sc_dev_time_tracker tt;
    uint8_t available_track_id_buffer[256];
    size_t available_track_id_count;
    struct can_echo echos[256];
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

            sc_tt_track(&s->tt, timestamp_us);

            if (!ac->candump && (ac->log_flags & LOG_FLAG_CAN_STATE)) {
                bool log = false;
                if (ac->log_on_change) {
                    log = ac->can_rx_errors_last != status->rx_errors ||
                        ac->can_tx_errors_last != status->tx_errors ||
                        ac->can_bus_state_last != status->bus_status;
                }
                else {
                    log = true;
                }

                ac->can_rx_errors_last = status->rx_errors;
                ac->can_tx_errors_last = status->tx_errors;
                ac->can_bus_state_last = status->bus_status;

                if (log) {
                    fprintf(stdout, "CAN rx errors=%u tx errors=%u bus=", status->rx_errors, status->tx_errors);
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
            }

            if (!ac->candump && (ac->log_flags & LOG_FLAG_USB_STATE)) {
                bool log = false;
                bool irq_queue_full = status->flags & SC_CAN_STATUS_FLAG_IRQ_QUEUE_FULL;
                bool desync = status->flags & SC_CAN_STATUS_FLAG_TXR_DESYNC;
                if (ac->log_on_change) {
                    log = ac->usb_rx_lost != rx_lost || 
                        ac->usb_tx_dropped != tx_dropped ||
                        irq_queue_full || desync;
                }
                else {
                    log = true;
                }

                ac->usb_rx_lost = rx_lost;
                ac->usb_tx_dropped = tx_dropped;

                if (log) {
                    fprintf(stdout, "CAN->USB rx lost=%u USB->CAN tx dropped=%u irqf=%u desync=%u\n", rx_lost, tx_dropped, irq_queue_full, desync);
                }
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

            if (ac->candump) {
                log_candump(ac, stdout, ts_us, can_id, rx->flags, rx->dlc, rx->data);
            }
            else {
                if (ac->log_flags & LOG_FLAG_RX_DT) {
                    int64_t dt_us = 0;
                    if (ac->rx_last_ts) {
                        dt_us = ts_us - ac->rx_last_ts;
                        if (dt_us < 0) {
                            fprintf(stderr, "WARN negative rx msg dt [us]: %lld\n", dt_us);
                        }
                    }

                    ac->rx_last_ts = ts_us;

                    fprintf(stdout, "rx delta %.3f [ms]\n", dt_us * 1e-3f);
                }

                if (ac->log_flags & LOG_FLAG_RX_MSG) {
                    fprintf(stdout, "RX ");
                    log_msg(ac, can_id, rx->flags, rx->dlc, rx->data);
                }
            }
        } break;
        case SC_MSG_CAN_TXR: {
            struct sc_msg_can_txr const* txr = (struct sc_msg_can_txr const*)msg;
            uint32_t timestamp_us = 0;
            uint64_t ts_us = 0;
            struct can_echo const* echo = NULL;

            if (msg->len < sizeof(*txr)) {
                fprintf(stderr, "malformed sc_msg_can_txr\n");
                return false;
            }

            timestamp_us = s->dev->dev_to_host32(txr->timestamp_us);
            ts_us = sc_tt_track(&s->tt, timestamp_us);
            echo = &s->echos[txr->track_id];

            if (s->available_track_id_count == _countof(s->available_track_id_buffer)) {
                fprintf(stderr, "TXR track id buffer overrun\n");
                return false;
            }

            // return track id
            s->available_track_id_buffer[s->available_track_id_count++] = txr->track_id;

            if (ac->candump) {
                log_candump(ac, stdout, ts_us, echo->can_id, echo->flags, echo->dlc, echo->data);
            }
            else {
                if (ac->log_flags & LOG_FLAG_TXR) {
                    if (txr->flags & SC_CAN_FRAME_FLAG_DRP) {
                        fprintf(stdout, "TXR %#08x was dropped @ %016llx\n", txr->track_id, ts_us);
                    }
                    else {
                        fprintf(stdout, "TXR %#08x was sent @ %016llx\n", txr->track_id, ts_us);
                    }
                }

                if ((ac->log_flags & LOG_FLAG_TX_MSG)) {
                    fprintf(stdout, "TX ");
                    log_msg(ac, echo->can_id, echo->flags, echo->dlc, echo->data);
                }
            }
        } break;
        default:
            fprintf(stderr, "WARN: unhandled msg id=%02x len=%u\n", msg->id, msg->len);
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


static int tx(struct app_ctx* ac, struct tx_job* job)
{
    uint32_t buffer[24];
    struct can_state* can_state = ac->priv;
    struct sc_msg_can_tx* tx = (struct sc_msg_can_tx*)buffer;
    uint16_t bytes = sizeof(*tx);
    uint8_t track_id = 0;
    uint8_t const data_len = dlc_to_len(job->dlc);
    struct can_echo* echo = NULL;
    int result = 0;
    
    if (!can_state->available_track_id_count) {
        goto exit_result;
    }

    track_id = can_state->available_track_id_buffer[--can_state->available_track_id_count];
    echo = &can_state->echos[track_id];

    echo->flags = job->flags;
    echo->can_id = job->can_id;
    echo->dlc = job->dlc;
    

    if (job->flags & SC_CAN_FRAME_FLAG_RTR) {

    }
    else {
        bytes += data_len;
        memcpy(tx->data, job->data, data_len);
        memcpy(echo->data, job->data, data_len);
    }

    if (bytes & (SC_MSG_CAN_LEN_MULTIPLE - 1)) {
        bytes += SC_MSG_CAN_LEN_MULTIPLE - (bytes & (SC_MSG_CAN_LEN_MULTIPLE - 1));
    }

    tx->id = SC_MSG_CAN_TX;
    tx->len = (uint8_t)bytes;
    tx->can_id = can_state->dev->dev_to_host32(job->can_id);
    tx->dlc = job->dlc;
    tx->flags = job->flags;
    tx->track_id = track_id;

    for (int i = 0; i < 2; ++i) {
        size_t added = 0;
        int error = sc_can_stream_tx_batch_add(
            can_state->stream,
            &(uint8_t*)tx,
            &bytes,
            1,
            &added);

        if (error) {
            fprintf(stderr, "sc_can_stream_tx failed: %s (%d)\n", sc_strerror(error), error);
            result = -1;
            goto exit_return_track_id;
        }

        if (added) {
            result = 1;
            goto exit_result;
        }

        // full
        error = sc_can_stream_tx_batch_end(can_state->stream);
        if (error) {
            fprintf(stderr, "sc_can_stream_tx_batch_end failed: %s (%d)\n", sc_strerror(error), error);
            result = -1;
            goto exit_return_track_id;
        }

        error = sc_can_stream_tx_batch_begin(can_state->stream);
        if (error) {
            fprintf(stderr, "sc_can_stream_tx_batch_begin failed: %s (%d)\n", sc_strerror(error), error);
            result = -1;
            goto exit_return_track_id;
        }
    }

exit_return_track_id:
    can_state->available_track_id_buffer[can_state->available_track_id_count++] = track_id;
exit_result:
    return result;
}

int run_single(struct app_ctx* ac)
{
    int error = SC_DLL_ERROR_NONE;
    struct can_state can_state;
    uint32_t count = 0;
    sc_version_t version;
    sc_cmd_ctx_t cmd_ctx;
    struct can_bit_timing_settings nominal_settings, data_settings;
    struct can_bit_timing_hw_contraints nominal_hw_constraints, data_hw_constraints;
    struct sc_msg_dev_info dev_info;
    struct sc_msg_can_info can_info;
    char serial_str[1 + sizeof(dev_info.sn_bytes) * 2] = { 0 };
    char name_str[1 + sizeof(dev_info.name_bytes)] = { 0 };

    memset(&can_state, 0, sizeof(can_state));
    memset(&cmd_ctx, 0, sizeof(cmd_ctx));
    memset(&version, 0, sizeof(version));

    for (size_t i = 0; i < _countof(can_state.available_track_id_buffer); ++i) {
        can_state.available_track_id_buffer[i] = (uint8_t)i;
    }

    can_state.available_track_id_count = _countof(can_state.available_track_id_buffer);

    sc_tt_init(&can_state.tt);

    ac->priv = &can_state;
    
    sc_init();

    sc_version(&version);

    fprintf(stdout, "DLL version %u.%u.%u.%u, commit '%s'\n", version.major, version.minor, version.patch, version.build, version.commit);

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

    if (!ac->candump) {
        fprintf(stdout, "%u " SC_NAME " devices found\n", count);
    }

    if (ac->device_index >= count) {
        fprintf(stdout, "Requested device index %u out of range\n", ac->device_index);
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    error = sc_dev_open_by_index(ac->device_index, &can_state.dev);
    if (error) {
        fprintf(stderr, "sc_dev_open for device index %u failed: %s (%d)\n", ac->device_index, sc_strerror(error), error);
        goto Exit;
    }

    if (!ac->candump) {
        fprintf(stdout, "cmd epp %#02x, can epp %#02x\n", can_state.dev->cmd_epp, can_state.dev->can_epp);
    }

    error = sc_cmd_ctx_init(&cmd_ctx, can_state.dev);
    if (error) {
        fprintf(stderr, "failed to initialize command context: %s (%d)\n", sc_strerror(error), error);
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
            fprintf(stderr, "failed to get device info: %s (%d)\n", sc_strerror(error), error);
            goto Exit;
        }

        if (rep_len < sizeof(dev_info)) {
            fprintf(stderr, "failed to get device info (short reponse)\n");
            error = -1;
            goto Exit;
        }

        memcpy(&dev_info, cmd_ctx.rx_buffer, sizeof(dev_info));

        dev_info.feat_perm = can_state.dev->dev_to_host16(dev_info.feat_perm);
        dev_info.feat_conf = can_state.dev->dev_to_host16(dev_info.feat_conf);

        if (!ac->candump) {
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
            fprintf(stderr, "failed to get CAN info: %s (%d)\n", sc_strerror(error), error);
            goto Exit;
        }

        if (rep_len < sizeof(can_info)) {
            fprintf(stderr, "failed to get can info (short reponse)\n");
            error = -1;
            goto Exit;
        }

        memcpy(&can_info, cmd_ctx.rx_buffer, sizeof(can_info));

        can_info.can_clk_hz = can_state.dev->dev_to_host32(can_info.can_clk_hz);
        can_info.msg_buffer_size = can_state.dev->dev_to_host16(can_info.msg_buffer_size);
        can_info.nmbt_brp_max = can_state.dev->dev_to_host16(can_info.nmbt_brp_max);
        can_info.nmbt_tseg1_max = can_state.dev->dev_to_host16(can_info.nmbt_tseg1_max);


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

        error = cia_fd_cbt_real(
            &nominal_hw_constraints, 
            &data_hw_constraints,
            &ac->nominal_user_constraints, 
            &ac->data_user_constraints,
            &nominal_settings,
            &data_settings);
        switch (error) {
        case CAN_BTRE_NO_SOLUTION:
            fprintf(stderr, "The chosen nominal/data bitrate/sjw cannot be configured on the device.\n");
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
        bt->brp = can_state.dev->dev_to_host16((uint16_t)nominal_settings.brp);
        bt->sjw = (uint8_t)nominal_settings.sjw;
        bt->tseg1 = can_state.dev->dev_to_host16((uint16_t)nominal_settings.tseg1);
        bt->tseg2 = (uint8_t)nominal_settings.tseg2;
        cmd_tx_ptr += bt->len;
        ++cmd_count;

        if (ac->fdf && ((dev_info.feat_perm | dev_info.feat_conf) & SC_FEATURE_FLAG_FDF)) {
            // CAN-FD capable & configured -> set data bitrate

            bt = (struct sc_msg_bittiming*)cmd_tx_ptr;
            memset(bt, 0, sizeof(*bt));
            bt->id = SC_MSG_DT_BITTIMING;
            bt->len = sizeof(*bt);
            bt->brp = can_state.dev->dev_to_host16((uint16_t)data_settings.brp);
            bt->sjw = (uint8_t)data_settings.sjw;
            bt->tseg1 = can_state.dev->dev_to_host16((uint16_t)data_settings.tseg1);
            bt->tseg2 = (uint8_t)data_settings.tseg2;
            cmd_tx_ptr += bt->len;
            ++cmd_count;
        }

        struct sc_msg_config* bus_on = (struct sc_msg_config*)cmd_tx_ptr;
        memset(bus_on, 0, sizeof(*bus_on));
        bus_on->id = SC_MSG_BUS;
        bus_on->len = sizeof(*bus_on);
        bus_on->arg = can_state.dev->dev_to_host16(1);
        cmd_tx_ptr += bus_on->len;
        ++cmd_count;

        uint16_t rep_len;
        error = sc_cmd_ctx_run(&cmd_ctx, (uint16_t)(cmd_tx_ptr - cmd_ctx.tx_buffer), &rep_len, CMD_TIMEOUT_MS);
        if (error) {
            fprintf(stderr, "failed to configure device: %s (%d)\n", sc_strerror(error), error);
            goto Exit;
        }


        if (rep_len < cmd_count * sizeof(struct sc_msg_error)) {
            fprintf(stderr, "failed to setup device  (short reponse)\n");
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

    error = sc_can_stream_init(can_state.dev, can_info.msg_buffer_size, ac, process_can, -1, &can_state.stream);
    if (error) {
        fprintf(stderr, "failed to initialize CAN stream: %s (%d)\n", sc_strerror(error), error);
        goto Exit;
    }

    can_state.stream->user_handle = ac->shutdown_event;

    DWORD timeout_ms = 0;
    bool was_full = false;

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    while (1) {
        error = sc_can_stream_rx(can_state.stream, timeout_ms);
        if (error) {
            if (SC_DLL_ERROR_USER_HANDLE_SIGNALED == error) {
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
            
            error = sc_can_stream_tx_batch_begin(can_state.stream);
            if (error) {
                fprintf(stderr, "sc_can_stream_tx_batch_begin failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }

            for (size_t i = 0; i < ac->tx_job_count; ++i) {
                struct tx_job* job = &ac->tx_jobs[i];

                if ((job->interval_ms >= 0 &&
                    (0 == job->last_tx_ts_ms || now - job->last_tx_ts_ms >= (unsigned)job->interval_ms)) ||
                    (job->interval_ms < 0 && job->count > 0)) {
                    const int job_count = job->count;

                    job->last_tx_ts_ms = now;

                    while (job->count > 0) {
                        int result = tx(ac, job);

                        switch (result) {
                        case -1:
                            goto Exit;
                        case 0:
                            if (!was_full) {
                                was_full = true;
                                fprintf(stderr, "ERROR: TX buffer full\n");
                                job->count = 0;
                            }
                            break;
                        case 1:
                            --job->count;
                            was_full = false;
                            break;
                        }
                    }

                    if (job->interval_ms >= 0) {
                        // reload count
                        job->count = job_count;

                        if (job->interval_ms >= 0 && (DWORD)job->interval_ms < timeout_ms) {
                            timeout_ms = job->interval_ms;
                        }
                    }
                }
            }

            error = sc_can_stream_tx_batch_end(can_state.stream);
            if (error) {
                fprintf(stderr, "sc_can_stream_tx_batch_end failed: %s (%d)\n", sc_strerror(error), error);
                goto Exit;
            }
        }
        else {
            timeout_ms = INFINITE;
        }
    }


Exit:
    sc_can_stream_uninit(can_state.stream);

    if (can_state.dev) {
        sc_cmd_ctx_uninit(&cmd_ctx);
        sc_dev_close(can_state.dev);
    }

    sc_uninit();

    return error;
}
