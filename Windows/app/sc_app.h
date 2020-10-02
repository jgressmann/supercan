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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>



struct sc_time_tracker {
    uint32_t ts_us_low_last;
    uint32_t ts_us_high;
    uint32_t initialized;
};

static inline void sc_tt_init(struct sc_time_tracker* tracker)
{
    memset(tracker, 0, sizeof(*tracker));
}

static inline uint64_t sc_track_ts(struct sc_time_tracker* tracker, uint32_t ts_us_current)
{    
    if (tracker->initialized) {
        uint32_t delta = ts_us_current - tracker->ts_us_low_last;
        if (delta < UINT32_MAX / 2) { // forward and playsible
            if (ts_us_current >= tracker->ts_us_low_last) {
                tracker->ts_us_low_last = ts_us_current;
                return ((uint64_t)tracker->ts_us_high) << 32 | ts_us_current;
            }
            else {
                ++tracker->ts_us_high;
                tracker->ts_us_low_last = ts_us_current;
                return ((uint64_t)tracker->ts_us_high) << 32 | ts_us_current;
            }
        }
        else {
            return ((uint64_t)tracker->ts_us_high - 1) << 32 | ts_us_current;
        }
    }
    else {
        tracker->initialized = 1;
        tracker->ts_us_low_last = ts_us_current;
        return ts_us_current;
    }
}
