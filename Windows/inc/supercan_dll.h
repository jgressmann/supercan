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


#ifndef SC_DLL_API
#   ifdef SC_STATIC
#      define SC_DLL_API SC_EXTERN_C
#   else
#      ifdef _MSC_VER
#          ifdef SC_DLL_EXPORTS
#              define SC_DLL_API __declspec(dllexport)
#          else
#              define SC_DLL_API _declspec(dllimport)
#          endif
#      else
#          error Define SC_DLL_API
#      endif
#   endif
#endif

#ifdef __cplusplus
extern "C" {
#endif



#define SC_DLL_ERROR_UNKNOWN                -1
#define SC_DLL_ERROR_NONE                   0
#define SC_DLL_ERROR_INVALID_PARAM          1
#define SC_DLL_ERROR_OUT_OF_MEM             2
#define SC_DLL_ERROR_DEV_COUNT_CHANGED      4
#define SC_DLL_ERROR_DEV_UNSUPPORTED        5   ///< not a SuperCAN device
#define SC_DLL_ERROR_VERSION_UNSUPPORTED    6   ///< unsupported SuperCAN protocol version
#define SC_DLL_ERROR_IO_PENDING             7   ///< I/O is underway
#define SC_DLL_ERROR_DEVICE_FAILURE         8   ///< USB related device failure, i.e. unplug
#define SC_DLL_ERROR_DEVICE_BUSY            9   ///< device not available (already opened)
#define SC_DLL_ERROR_ABORTED                10  ///< request was aborted / canceled
#define SC_DLL_ERROR_DEV_NOT_IMPLEMENTED    11  ///< device doesn't implement requested feature
#define SC_DLL_ERROR_PROTO_VIOLATION        12  ///< malformed data buffer
#define SC_DLL_ERROR_SEQ_VIOLATION          13  ///< jumbled CAN message sequence
#define SC_DLL_ERROR_REASSEMBLY_SPACE       14  ///< insufficient message reassembly buffer space
#define SC_DLL_ERROR_TIMEOUT                15  ///< timeout
#define SC_DLL_ERROR_AGAIN                  16  ///< try again

typedef uint16_t(*sc_dev_to_host16)(uint16_t value);
typedef uint32_t(*sc_dev_to_host32)(uint32_t value);

typedef struct sc_dev {
    sc_dev_to_host16 dev_to_host16;
    sc_dev_to_host32 dev_to_host32;
    uint16_t cmd_buffer_size;
    uint16_t epp_size;
    uint8_t cmd_epp;
    uint8_t can_epp;
} sc_dev_t;

/** Initializes the library 
 *
 * This function is not thread-safe.
 */
SC_DLL_API void sc_init(void);

/** Uninitializes the library 
 *
 * This function is not thread-safe.
 */
SC_DLL_API void sc_uninit(void);

/** Returns a textual description of the error code */
SC_DLL_API char const* sc_strerror(int error);

/** Scans the system for devices. 
 * 
 * This function is not thread-safe.
 */
SC_DLL_API int sc_dev_scan(void);

/** Gets the number of devices found. */
SC_DLL_API int sc_dev_count(uint32_t* count);

/** Open device by index. */
SC_DLL_API int sc_dev_open(uint32_t index, sc_dev_t** dev);

/** Closes the open device. */
SC_DLL_API void sc_dev_close(sc_dev_t* dev);

/** Submits a formated buffer down the USB stack to read from the device (bulk IN). 
 * 
 * \param pipe  USB pipe 1-15
 * \param buffer buffer to fill with data returned from device
 * \param bytes capacity of the buffer
 * \param ov    standard Windows API overlapped
 * 
 * \returns error code
 * \see WinUsb_ReadPipe
 */
SC_DLL_API int sc_dev_read(sc_dev_t *dev, uint8_t pipe, void* buffer, ULONG bytes, OVERLAPPED* ov);

/** Submits a formated buffer down the USB stack for write to the device (bulk OUT).
 *
 * \param pipe  USB pipe 1-15
 * \param buffer buffer to fill with data returned from device
 * \param bytes capacity of the buffer
 * \param ov    standard Windows API overlapped
 *
 * \returns error code
 * \see WinUsb_WritePipe
 */
SC_DLL_API int sc_dev_write(sc_dev_t *dev, uint8_t pipe, void const *buffer, ULONG bytes, OVERLAPPED* ov);

/** Retrieves result of for a read/write.
 *
 * \param bytes bytes transferred
 * \param ov    standard Windows API overlapped
 * \param timeout    milliseconds to wait for. Pass INFINITE to wait indefinitely.
 *
 * \returns error code
 * \see WinUsb_GetOverlappedResult
 */
SC_DLL_API int sc_dev_result(sc_dev_t *dev, DWORD* bytes, OVERLAPPED *ov, int timeout_ms);

/** Cancels an asynchronous operation.
 *
 * \param ov    standard Windows API overlapped
 *
 * \returns error code
 * \see CancelIoEx
 */
SC_DLL_API int sc_dev_cancel(sc_dev_t *dev, OVERLAPPED *ov);




typedef void* sc_can_stream_t;
typedef int (*sc_can_stream_rx_callback)(void* ctx, void const* ptr, uint16_t bytes);

SC_DLL_API void sc_can_stream_uninit(sc_can_stream_t stream);
SC_DLL_API int sc_can_stream_init(
    sc_dev_t* dev, 
    DWORD buffer_size, 
    void *ctx,
    sc_can_stream_rx_callback callback,
    int rreqs, 
    sc_can_stream_t* stream);

SC_DLL_API int sc_can_stream_reset(
    sc_can_stream_t stream);

SC_DLL_API int sc_can_stream_tx(
    sc_can_stream_t stream,
    void const* ptr, 
    size_t bytes, 
    DWORD timeout_ms,
    size_t* written);


SC_DLL_API int sc_can_stream_run(
    sc_can_stream_t stream,
    DWORD timeout_ms);


#ifdef __cplusplus
}
#endif