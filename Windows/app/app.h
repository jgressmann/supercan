/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2021 Jean Gressmann <jean@0x42.de>
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

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "supercan_winapi.h"
#include "supercan_dll.h"
#include "can_bit_timing.h"

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef _countof
#   define _countof(x) (sizeof(x)/sizeof((x)[0]))
#endif



#define LOG_FLAG_RX_DT      0x00000001
#define LOG_FLAG_RX_MSG     0x00000002
#define LOG_FLAG_CAN_STATE  0x00000004
#define LOG_FLAG_TX_MSG     0x00000008
#define LOG_FLAG_TXR        0x00000010
#define LOG_FLAG_USB_STATE  0x00000020


#ifdef __cplusplus
extern "C" {
#endif


struct tx_job {
    uint64_t last_tx_ts_ms;
    uint32_t can_id;
    int interval_ms;
    uint8_t flags;
    uint8_t dlc;
    uint8_t data[64];
};

struct app_ctx {
    struct can_bit_timing_constraints_real nominal_user_constraints, data_user_constraints;
    struct tx_job tx_jobs[8];
    uint64_t rx_last_ts;
    HANDLE shutdown_event;
    void* priv;
    unsigned log_flags;
    unsigned tx_job_count;
    unsigned device_index;
    int can_bus_state_last;
    int can_tx_errors_last;
    int can_rx_errors_last;
    int usb_rx_lost;
    int usb_tx_dropped;
    bool rx_has_xtd_frame;
    bool rx_has_fdf_frame;
    bool fdf;
    bool config;
    bool log_on_change;
    bool candump;
};

static inline uint8_t dlc_to_len(uint8_t dlc)
{
    static const uint8_t map[16] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64
    };
    return map[dlc & 0xf];
}

static inline uint8_t len_to_dlc(uint8_t len)
{
    if (len <= 8) {
        return len;
    }

    if (len <= 12) {
        return 9;
    }

    if (len <= 16) {
        return 10;
    }

    if (len <= 20) {
        return 11;
    }

    if (len <= 24) {
        return 12;
    }

    if (len <= 32) {
        return 13;
    }

    if (len <= 48) {
        return 14;
    }

    return 15;
}


static inline bool is_false(char const* str)
{
    return
        0 == _stricmp(str, "0") ||
        0 == _stricmp(str, "false") ||
        0 == _stricmp(str, "no") ||
        0 == _stricmp(str, "off");
}

void log_msg(
    struct app_ctx* ctx,
    uint32_t can_id,
    uint8_t flags,
    uint8_t dlc,
    uint8_t const* data);

void log_candump(
    struct app_ctx* ctx,
    FILE* f,
    uint64_t timestamp_us,
    uint32_t can_id,
    uint8_t flags,
    uint8_t dlc,
    uint8_t const* data);

#ifdef __cplusplus
} // extern "C"
#endif
