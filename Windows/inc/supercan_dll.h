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

#include <stdint.h>


#ifndef SC_DLL_API
#   ifdef SC_STATIC
#      define SC_DLL_API
#   else
#      ifdef _MSC_VER
#          ifdef SC_DLL_EXPORTS
#              define SC_DLL_API __declspec(dllexport)
#          else
#              define SC_DLL_API __declspec(dllimport)
#          endif
#      else
#          error Define SC_DLL_API
#      endif
#   endif
#endif

#ifdef _MSC_VER
#   define SC_DEPRECATED __declspec(deprecated)
#else 
#   define SC_DEPRECATED 
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SC_DLL_VERSION_MAJOR 0
#define SC_DLL_VERSION_MINOR 5
#define SC_DLL_VERSION_PATCH 1



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
#define SC_DLL_ERROR_AGAIN                  16  ///< try again (later)
#define SC_DLL_ERROR_BUFFER_TOO_SMALL       17  ///< buffer too small
#define SC_DLL_ERROR_USER_HANDLE_SIGNALED   18  ///< user provided handle was signaled
#define SC_DLL_ERROR_ACCESS_DENIED          19  ///< access denied
#define SC_DLL_ERROR_INVALID_OPERATION      20  ///< operation not possible in current state
#define SC_DLL_ERROR_DEVICE_GONE            21  ///< device was removed from the system

typedef struct sc_version {
    char const* commit;
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t build;
} sc_version_t;

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


typedef enum {
    SC_DLL_LOG_LEVEL_OFF = -1,
    SC_DLL_LOG_LEVEL_ERROR = 0x00,
    SC_DLL_LOG_LEVEL_WARNING = 0x10,
    SC_DLL_LOG_LEVEL_INFO = 0x20,
    SC_DLL_LOG_LEVEL_DEBUG = 0x30,
    SC_DLL_LOG_LEVEL_DEBUG1 = SC_DLL_LOG_LEVEL_DEBUG,
    SC_DLL_LOG_LEVEL_DEBUG2 = 0x40,
    SC_DLL_LOG_LEVEL_DEBUG3 = 0x50
} sc_log_level_t;


/** Log callback prototype
 *
 * \param ctx   user context
 * \param msg   log message, always zero-terminated
 * \param size  size of log message in bytes, excluding trailing zero
 */
typedef void (*sc_log_callback_t)(void* ctx, int level, char const* msg, size_t size);


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

/** Retrieve library version
 *
 * This function is thread-safe.
 */
SC_DLL_API void sc_version(sc_version_t* version);


/** Set library log level
 *
 * This function is not thread-safe.
 * 
 * After initialization, logging is disabled (off).
 */
SC_DLL_API void sc_log_set_level(int level);

/** Set library log callback
 *
 * This function is not thread-safe.
 */
SC_DLL_API void sc_log_set_callback(void* ctx, sc_log_callback_t callback);

/** Scans the system for devices.
 *
 * This function is not thread-safe.
 */
SC_DLL_API int sc_dev_scan(void);

/** Gets the number of devices found. */
SC_DLL_API int sc_dev_count(uint32_t* count);

/** Gets the device identifier.
 *
 * \param index device index
 * \param buf buffer to receive device identifier
 * \param (inout) buffer capacity on call, length of the string on return,
 *        not including the terminating \0 character.
 *
 * \returns error code
 */
SC_DLL_API int sc_dev_id_unicode(uint32_t index, wchar_t* buf, size_t* len);

/** Open device by index. */
SC_DLL_API int sc_dev_open_by_index(uint32_t index, sc_dev_t** dev);

/** Open device by identifier. */
SC_DLL_API int sc_dev_open_by_id(wchar_t const* id, sc_dev_t** dev);

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

/** Sets device log level 
 * 
 * After initialization, device logging is disabled (off).
 */
SC_DLL_API int sc_dev_log_set_level(sc_dev_t* dev, int level);

/** Sets device log callback */
SC_DLL_API int sc_dev_log_set_callback(sc_dev_t* dev, void* ctx, sc_log_callback_t callback);


typedef struct sc_cmd_ctx {
    sc_dev_t* dev;
    PUCHAR tx_buffer;   // dev->cmd_buffer_size
    PUCHAR rx_buffer;   // dev->cmd_buffer_size
    OVERLAPPED tx_ov;
    OVERLAPPED rx_ov;
} sc_cmd_ctx_t;


/** Uninitializes cmd state */
SC_DLL_API void sc_cmd_ctx_uninit(sc_cmd_ctx_t* ctx);
/** Initializes cmd state */
SC_DLL_API int sc_cmd_ctx_init(sc_cmd_ctx_t* ctx, sc_dev_t* dev);

/** Sends command to device and processes result */
SC_DLL_API int sc_cmd_ctx_run(
    sc_cmd_ctx_t* ctx,
    uint16_t cmd_bytes,
    uint16_t* reply_bytes,
    DWORD timeout_ms);


typedef struct sc_can_stream {
    HANDLE user_handle;     ///< optional user handle to wait on in sc_can_stream_rx
    uint16_t tx_capacity;   ///< capacity of transmit buffer
    uint16_t reserved;
} sc_can_stream_t;

typedef int (*sc_can_stream_rx_callback)(void* ctx, void const* ptr, uint16_t bytes);

/** Uninitializes the stream
 *
 * \param stream object to uninitialize, can be NULL
 *
 */
SC_DLL_API void sc_can_stream_uninit(sc_can_stream_t *stream);

/** Initializes the stream
 *
 * \param dev   device
 * \param buffer_size   size of the device buffer as retrieved
 *      through sc_msg_can_info.
 * \param ctx   context passed to rx callback
 * \param callback  callback to invoke on message reception
 * \param rreqs  number of read requests to submit to the USB stack
 *      pass 0 to use default.
 * \param [out] stream
 *
 * \returns error code
 */
SC_DLL_API int sc_can_stream_init(
    sc_dev_t* dev,
    DWORD buffer_size,
    void *ctx,
    sc_can_stream_rx_callback callback,
    int rreqs,
    sc_can_stream_t** stream);


/** Starts a new batched transmit
 *
 * \param stream    CAN stream
 *
 * \returns error code
 */
SC_DLL_API int sc_can_stream_tx_batch_begin(sc_can_stream_t* stream);

/** Adds buffers to the current transmit batch
 *
 * \param stream    CAN stream
 * \param buffers   Array of buffers
 * \param sizes     Array of sizes (one for each buffer)
 * \param count     Number of buffers/sizes in the array
 * \param added     Output number of buffers added
 *
 * \returns error code 
 */
SC_DLL_API int sc_can_stream_tx_batch_add(
    sc_can_stream_t* stream,
    uint8_t const** buffers,
    uint16_t const* sizes,
    size_t count,
    size_t* added);

/** Finishes the current batch
 *
 * Is is legal to end an empty batch in which case
 * no transmit will be started.
 * 
 * \param stream    CAN stream
 *
 * \returns error code
 */
SC_DLL_API int sc_can_stream_tx_batch_end(sc_can_stream_t* stream);

/** Transmit a frame
 *
 * \param stream    CAN stream
 * \param ptr       Pointer to sc_msg_can_tx aligned to SC_MSG_CAN_LEN_MULTIPLE
 * \param bytes     Bytes in buffer
 *
 * \returns error code
 */
SC_DLL_API int sc_can_stream_tx(
    sc_can_stream_t* stream,
    uint8_t const* ptr,
    uint16_t bytes);

/** Receives CAN messages
 *
 * Calls this function repeatedly to implement the message loop.
 *
 * \param stream        CAN stream
 * \param timeout_ms    timeout in milliseconds, argument to WaitForMultipleObjects.
 * \returns error code
 */
SC_DLL_API int sc_can_stream_rx(sc_can_stream_t* stream, DWORD timeout_ms);


/** Retrieves the stream's next rx wait handle
 *
 * Call this function prior to sc_can_stream_rx to get the wait handle.
 * Wait for the handle to become signaled, then call sc_can_stream_rx.
 * 
 * NOTE: the wait handle may change after a call to sc_can_stream_rx so
 * make sure to call this function again.
 *
 * \param stream        CAN stream
 * \param handle        the next wait handle to wait upon.
 * \returns error code
 */
SC_DLL_API int sc_can_stream_rx_next_wait_handle(sc_can_stream_t* stream, HANDLE* handle);

/** Process the stream's signaled wait handle
 *
 * NOTE: This function doesn't check if the handle is in fact signaled.
 * NOTE: It is up the the caller to ensure the handle is in the proper
 * NOTE: state prior to the call.
 *
 * \param stream        CAN stream
 
 * \returns error code
 */
SC_DLL_API int sc_can_stream_rx_process_signaled_wait_handle(sc_can_stream_t* stream);

#ifdef __cplusplus
}
#endif
