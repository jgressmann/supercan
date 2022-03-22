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

#pragma once

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4200) // zero sized array in struct
#endif

#define SC_FACILITY 0x0200
#define SC_HRESULT_FROM_ERROR(x) MAKE_HRESULT(1, SC_FACILITY, (int8_t)x)

#define SC_SRV_VERSION_MAJOR 1
#define SC_SRV_VERSION_MINOR 0
#define SC_SRV_VERSION_PATCH 0

#ifdef __cplusplus
extern "C" {
#endif

/* These structures follow the SuperCAN protocol but are in always 
 * in host byte order.
 */

enum sc_can_data_type {
    SC_CAN_DATA_TYPE_NONE,
    SC_CAN_DATA_TYPE_STATUS,
    SC_CAN_DATA_TYPE_RX,
    SC_CAN_DATA_TYPE_TX,
    SC_CAN_DATA_TYPE_ERROR,
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
    uint64_t timestamp_us;
    uint8_t data[64];
};

struct sc_mm_can_tx {
    uint8_t type;
    uint8_t dlc;
    uint8_t flags;
    uint8_t echo;           ///< TX echo (ignore track_id)
    uint32_t track_id;
    uint32_t can_id;
    uint64_t timestamp_us;
    uint8_t data[64];
};

struct sc_mm_can_status {
    uint8_t type;
    uint8_t reserved;
    uint8_t flags;              ///< CAN bus status flags
    uint8_t bus_status;
    uint16_t rx_lost;           ///< messages CAN -> USB lost since last time due to full rx fifo
    uint16_t tx_dropped;        ///< messages USB -> CAN dropped since last time due of full tx fifo
    uint64_t timestamp_us;
    uint8_t rx_errors;          ///< CAN rx error counter
    uint8_t tx_errors;          ///< CAN tx error counter
    uint8_t rx_fifo_size;       ///< CAN rx fifo fill state
    uint8_t tx_fifo_size;       ///< CAN tx fifo fill state
};

struct sc_mm_can_error {
    uint8_t type;
    uint8_t error;
    uint8_t flags;
    uint8_t reserved[5];
    uint64_t timestamp_us;
};

typedef union sc_can_mm_slot {
    struct sc_mm_header hdr;
    struct sc_mm_can_rx rx;
    struct sc_mm_can_tx tx;
    struct sc_mm_can_status status;
    struct sc_mm_can_error error;
} sc_can_mm_slot_t;

enum sc_mm_flags {
    /** An error has occurred (see sc_can_mm_header::error) 
      *
      * This flag is sticky and will remain until the device is 
      * is taken off the bus.
      */
    SC_MM_FLAG_ERROR = 0x1,
    
    /** The device has been taken on the bus
      * 
      * NOTE: this flag remains set until the device is explicitly taken off the bus.
      */
    SC_MM_FLAG_BUS_ON = 0x2,
};

struct sc_can_mm_header {
    /* Clients should atomically swap with 0 to get lost_* 
     * 
     * Only valid for RX ring
     */
    volatile uint32_t lost_rx;      ///< CAN RX frames lost
    volatile uint32_t lost_status;  ///< CAN status messages lost
    volatile uint32_t lost_tx;      ///< CAN TX receipt/echo messages lost 
    volatile uint32_t lost_error;   ///< CAN error messages lost 
                                    
    volatile uint32_t get_index;    // not an index, need to be %'d
    volatile uint32_t put_index;    // not an index, need to be %'d
    volatile int32_t error;         // device error
    volatile uint32_t flags;        // flags
    volatile uint32_t reserved1[8]; // reserved for now
    sc_can_mm_slot_t elements[0];
};

#ifdef __cplusplus
}
#endif

#ifdef _MSC_VER
#   pragma warning(pop)
#endif
