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

#include "supercan_winapi.h"

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4200)
#endif

#define SC_FACILITY 0x0200
#define SC_HRESULT_FROM_ERROR(x) MAKE_HRESULT(1, SC_FACILITY, (int8_t)x)

#ifdef __cplusplus
extern "C" {
#endif
//
///** Windows native byte order received frame
// *
// */
//typedef struct sc_can_rx_frame {
//    uint32_t can_id;
//    uint32_t timestamp_us;
//    uint8_t dlc;
//    uint8_t flags;
//    uint8_t reserved[2];
//    uint8_t data[64];
//} sc_can_rx_frame_t;

enum sc_can_data_type {
    SC_CAN_DATA_TYPE_NONE,
    SC_CAN_DATA_TYPE_STATUS,
    //SC_CAN_DATA_TYPE_ERROR,
    SC_CAN_DATA_TYPE_RX,
    SC_CAN_DATA_TYPE_TX,
    SC_CAN_DATA_TYPE_TXR
};

struct sc_mm_header {
    uint8_t type;
};

struct sc_mm_can_rx {
    uint8_t type;
    uint8_t dlc;
    uint8_t flags;
    uint8_t reserved;
    uint32_t can_id;
    uint32_t timestamp_us;
    uint8_t data[64];
};

struct sc_mm_can_tx {
    uint8_t type;
    uint8_t dlc;
    uint8_t flags;
    uint8_t reserved;
    uint32_t track_id;
    uint32_t can_id;
    uint8_t data[64];
};

struct sc_mm_can_txr {
    uint8_t type;
    uint8_t reserved[2];
    uint8_t flags;
    uint32_t track_id;
    uint32_t timestamp_us;
};

struct sc_mm_can_status {
    uint8_t type;
    uint8_t reserved;
    uint8_t flags;              ///< CAN bus status flags
    uint8_t bus_status;
    uint32_t timestamp_us;
    uint16_t rx_lost;           ///< messages CAN -> USB lost since last time due to full rx fifo
    uint16_t tx_dropped;        ///< messages USB -> CAN dropped since last time due of full tx fifo
    uint8_t rx_errors;          ///< CAN rx error counter
    uint8_t tx_errors;          ///< CAN tx error counter
    uint8_t rx_fifo_size;       ///< CAN rx fifo fill state
    uint8_t tx_fifo_size;       ///< CAN tx fifo fill state
};

//struct sc_can_mm_slot {
//    
//    struct sc_can_frame data;
//    /*union sc_can_mm_slot {
//        struct sc_can_tx_frame tx;
//        struct sc_can_tx_frame txr;
//        struct sc_can_rx_frame rx;
//    } data;*/
//};

//typedef struct sc_can_data sc_can_mm_slot_t;
typedef union sc_can_mm_slot {
    struct sc_mm_header hdr;
    struct sc_mm_can_rx rx;
    struct sc_mm_can_tx tx;
    struct sc_mm_can_txr txr;
    struct sc_mm_can_status status;
} sc_can_mm_slot_t;

struct sc_can_mm_header {
    volatile uint64_t rx_lost;
    volatile uint64_t txr_lost;
    volatile uint32_t get_index; // not an index, need to %
    volatile uint32_t put_index; // not an index, need to %
    //volatile uint32_t generation;  // internal, do not use
    //uint8_t reserved[4];
    sc_can_mm_slot_t slots[0];
};

#ifdef __cplusplus
}
#endif

#ifdef _MSC_VER
#   pragma warning(pop)
#endif
