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

#ifdef _MSC_VER
#   pragma warning(push)
#   pragma warning(disable: 4200) // zero sized array in struct
#endif

#define SC_FACILITY 0x0200
#define SC_HRESULT_FROM_ERROR(x) MAKE_HRESULT(1, SC_FACILITY, (int8_t)x)

#define SC_SRV_VERSION_MAJOR 0
#define SC_SRV_VERSION_MINOR 6
#define SC_SRV_VERSION_PATCH 3

#ifdef __cplusplus
extern "C" {
#endif

/* These structures follow the SuperCAN protocol but are in always 
 * in host byte order.
 */

enum sc_mm_data_type {
    SC_MM_DATA_TYPE_NONE,
    SC_MM_DATA_TYPE_CAN_STATUS,
    SC_MM_DATA_TYPE_CAN_RX,
    SC_MM_DATA_TYPE_CAN_TX,
    SC_MM_DATA_TYPE_CAN_ERROR,


    SC_MM_DATA_TYPE_LOG_DATA = 0x10,
};

enum sc_log_data_flags {
    SC_LOG_DATA_FLAG_MORE = 0x1     ///< more of same log message follows
};

enum sc_log_data_src {
    SC_LOG_DATA_SRC_DLL,            ///< message originated in SC DLL
    SC_LOG_DATA_SRC_SRV             ///< message originated in SC COM server
};

#define SC_LOG_DATA_BUFFER_SIZE 72
#define SC_MM_ELEMENT_SIZE      88


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
    uint32_t reserved;
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

struct sc_mm_log_data {
    uint8_t type;
    int8_t level;
    uint8_t flags;
    uint8_t bytes;
    uint8_t src;
    uint8_t reserved0[3];
    uint64_t reserved1;
    uint8_t data[SC_LOG_DATA_BUFFER_SIZE];           ///< UTF-8
};

typedef union sc_can_mm_slot {
    struct sc_mm_header hdr;
    struct sc_mm_can_rx rx;
    struct sc_mm_can_tx tx;
    struct sc_mm_can_status status;
    struct sc_mm_can_error error;
    struct sc_mm_log_data log_data;
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

    /** The device is gone (failure or unplug) */
     SC_MM_FLAG_GONE = 0x4,
};

struct sc_can_mm_header {
    /* Clients should atomically swap with 0 to get lost_* 
     * 
     * Only valid for RX ring
     */
    volatile uint32_t can_lost_rx;      ///< CAN RX frames lost
    volatile uint32_t can_lost_status;  ///< CAN status messages lost
    volatile uint32_t can_lost_tx;      ///< CAN TX receipt/echo messages lost 
    volatile uint32_t can_lost_error;   ///< CAN error messages lost 
                                    
    volatile uint32_t get_index;        ///< not an index, need to be %'d
    volatile uint32_t put_index;        ///< not an index, need to be %'d
    volatile int32_t error;             ///< device error
    volatile uint32_t flags;            ///< flags
    volatile uint32_t log_lost;         ///< log messages lost
    volatile uint32_t generation;       ///< device generation, incremented each time the device is re-discovered
    volatile uint32_t reserved1[6];     // reserved for now
    sc_can_mm_slot_t elements[0];
};

enum {
    sc_static_assert_sizeof_sc_mm_can_rx_fits = sizeof(int[sizeof(struct sc_mm_can_rx) <= SC_MM_ELEMENT_SIZE ? 1 : -1]),
    sc_static_assert_sizeof_sc_mm_can_tx_fits = sizeof(int[sizeof(struct sc_mm_can_tx) == SC_MM_ELEMENT_SIZE ? 1 : -1]),
    sc_static_assert_sizeof_sc_mm_can_status_fits = sizeof(int[sizeof(struct sc_mm_can_status) <= SC_MM_ELEMENT_SIZE ? 1 : -1]),
    sc_static_assert_sizeof_sc_mm_can_error_fits = sizeof(int[sizeof(struct sc_mm_can_error) <= SC_MM_ELEMENT_SIZE ? 1 : -1]),
    sc_static_assert_sizeof_sc_mm_log_data_fits = sizeof(int[sizeof(struct sc_mm_log_data) == SC_MM_ELEMENT_SIZE ? 1 : -1]),
};

#ifdef __cplusplus
}
#endif

#ifdef _MSC_VER
#   pragma warning(pop)
#endif
