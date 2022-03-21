/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2021-2022 Jean Gressmann <jean@0x42.de>
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

#include <stdint.h>

void log_msg(
    struct app_ctx* ctx,
    uint32_t can_id,
    uint8_t flags,
    uint8_t dlc,
    uint8_t const* data)
{
    fprintf(stdout, "%s %s %s %s ",
        (flags & SC_CAN_FRAME_FLAG_EXT) == SC_CAN_FRAME_FLAG_EXT ? "XTD" : "   ",
        (flags & SC_CAN_FRAME_FLAG_FDF) == SC_CAN_FRAME_FLAG_FDF ? "FDF" : "   ",
        (flags & (SC_CAN_FRAME_FLAG_FDF | SC_CAN_FRAME_FLAG_BRS)) == (SC_CAN_FRAME_FLAG_FDF | SC_CAN_FRAME_FLAG_BRS) ? "BRS" : "   ",
        (flags & (SC_CAN_FRAME_FLAG_FDF | SC_CAN_FRAME_FLAG_ESI)) == (SC_CAN_FRAME_FLAG_FDF | SC_CAN_FRAME_FLAG_ESI) ? "ESI" : "   "
        );

    if (flags & SC_CAN_FRAME_FLAG_EXT) {
        ctx->rx_has_xtd_frame = true;
        can_id &= 0x1fffffff;
    }
    else {
        can_id &= 0x7ff;
    }

    if (flags & SC_CAN_FRAME_FLAG_FDF) {
        ctx->rx_has_fdf_frame = true;
        dlc &= 0xf;
    }
    else {
        if (dlc > 8) {
            dlc = 8;
        }
    }

    uint8_t len = dlc_to_len(dlc);

    if (ctx->rx_has_xtd_frame) {
        fprintf(stdout, "%8X ", can_id);
    }
    else {
        fprintf(stdout, "%3X ", can_id);
    }

    if (ctx->rx_has_fdf_frame) {
        fprintf(stdout, "[%02u] ", len);
    }
    else {
        fprintf(stdout, "[%u] ", len);
    }

    if (flags & SC_CAN_FRAME_FLAG_RTR) {
        fprintf(stdout, "RTR");
    }
    else {
        for (uint8_t i = 0; i < len; ++i) {
            fprintf(stdout, "%02X ", data[i]);
        }
    }
    fputc('\n', stdout);
}

void log_candump(
    struct app_ctx* ctx,
    FILE* f,
    uint64_t timestamp_us,
    uint32_t can_id,
    uint8_t flags,
    uint8_t dlc,
    uint8_t const* data)
{
    uint64_t s = timestamp_us / 1000000u;
    timestamp_us -= s * 1000000u;
    fprintf(f, "(%010lu.%06lu) can%u ", (unsigned long)s, (unsigned long)timestamp_us, ctx->device_index);

    if (flags & SC_CAN_FRAME_FLAG_EXT) {
        fprintf(f, "%08X#", can_id);
    }
    else {
        fprintf(f, "%03X#", can_id);
    }

    if (flags & SC_CAN_FRAME_FLAG_FDF) {
        fprintf(f, "#%c", (flags & SC_CAN_FRAME_FLAG_BRS) ? '1' : '0');
    }
    else 

    if (flags & SC_CAN_FRAME_FLAG_RTR) {
        fprintf(f, "RTR\n");
    }
    else {
        unsigned len = dlc_to_len(dlc);
        for (unsigned i = 0; i < len; ++i) {
            fprintf(f, "%02X", data[i]);
        }

        fprintf(f, "\n");
    }
}
