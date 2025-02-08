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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winusb.h>
#include <usb.h>
#include <cfgmgr32.h>

//
// Define below GUIDs
//
#include <initguid.h>
#include <devpkey.h>

//
// Device Interface GUID.
// Used by all WinUsb devices that this application talks to.
// Must match "DeviceInterfaceGUIDs" registry value specified in the INF file.
// f4ef82e0-dc07-4f21-8660-ae50cb3149c9
//
DEFINE_GUID(GUID_DEVINTERFACE_supercan,
    0xf4ef82e0, 0xdc07, 0x4f21, 0x86, 0x60, 0xae, 0x50, 0xcb, 0x31, 0x49, 0xc9);

#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <supercan_dll.h>
#include <supercan_winapi.h>
#include <stdio.h>


#include "commit.h"

#if REG_DWORD == REG_DWORD_LITTLE_ENDIAN
#   define NATIVE_BYTE_ORDER SC_BYTE_ORDER_LE
#else
#   define NATIVE_BYTE_ORDER SC_BYTE_ORDER_BE
#endif

#define SC_CAN_STREAM_MAX_RX_WAIT_HANDLES 64
#define SC_CAN_STREAM_DEFAULT_RX_WAIT_HANDLES 32

#define LOG_LIB(level, ...) \
	do { \
        if (level <= s_LogLevel) {\
		    char buf[256]; \
		    int chars = _snprintf_s(buf, sizeof(buf), _TRUNCATE, __VA_ARGS__); \
            s_LogCallback(s_LogCtx, level, buf, chars); \
        } \
	} while (0)


#define LOG_DEV(dev, level, ...) \
	do { \
        if (level <= (dev)->log_level) { \
		    char buf[256]; \
		    int chars = _snprintf_s(buf, sizeof(buf), _TRUNCATE, __VA_ARGS__); \
            (dev)->log_callback((dev)->log_ctx, level, buf, chars); \
        } \
	} while (0)


#define LOG_LIB_ERROR(...) LOG_LIB(SC_DLL_LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_LIB_WARN(...) LOG_LIB(SC_DLL_LOG_LEVEL_WARNING, __VA_ARGS__)
#define LOG_LIB_INFO(...) LOG_LIB(SC_DLL_LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_LIB_DEBUG1(...) LOG_LIB(SC_DLL_LOG_LEVEL_DEBUG, __VA_ARGS__)

#define LOG_DEV_ERROR(dev, ...) LOG_DEV(dev, SC_DLL_LOG_LEVEL_ERROR, __VA_ARGS__)
#define LOG_DEV_WARN(dev, ...) LOG_DEV(dev, SC_DLL_LOG_LEVEL_WARNING, __VA_ARGS__)
#define LOG_DEV_INFO(dev, ...) LOG_DEV(dev, SC_DLL_LOG_LEVEL_INFO, __VA_ARGS__)
#define LOG_DEV_DEBUG1(dev, ...) LOG_DEV(dev, SC_DLL_LOG_LEVEL_DEBUG1, __VA_ARGS__)
#define LOG_DEV_DEBUG2(dev, ...) LOG_DEV(dev, SC_DLL_LOG_LEVEL_DEBUG2, __VA_ARGS__)
#define LOG_DEV_DEBUG3(dev, ...) LOG_DEV(dev, SC_DLL_LOG_LEVEL_DEBUG3, __VA_ARGS__)


#define SC_DLL_VERSION_BUILD 0
#define SC_CMD_TIMEOUT_MS 3000
#define SC_STREAM_TIMEOUT_MS 5000

static void Nop(void * dev, int level, char const* msg, size_t size)
{
    (void)dev;
    (void)level;
    (void)msg;
    (void)size;
}

static int s_LogLevel = SC_DLL_LOG_LEVEL_OFF;
static void* s_LogCtx = NULL;
static sc_log_callback_t s_LogCallback = &Nop;



static struct sc_data {
    wchar_t *dev_list;
    size_t *dev_name_indices;
    size_t dev_count;
} sc_data;

static inline uint16_t Nop16(uint16_t value)
{
    return value;
}

static inline uint32_t Nop32(uint32_t value)
{
    return value;
}

static inline uint16_t Swap16(uint16_t value)
{
    return _byteswap_ushort(value);
}

static inline uint32_t Swap32(uint32_t value)
{
    return _byteswap_ulong(value);
}

struct sc_dev_ex {
    sc_dev_t exposed;
    HANDLE dev_handle;
    WINUSB_INTERFACE_HANDLE usb_handle;
    wchar_t* path;
    void* log_ctx;
    sc_log_callback_t log_callback;
    int log_level;
};

struct sc_stream {
    sc_can_stream_t exposed;
    struct sc_dev_ex* dev;
    void* ctx;
    sc_can_stream_rx_callback rx_callback;
    PUCHAR rx_buffers;
    PUCHAR tx_buffers;
    OVERLAPPED* rx_ovs;
    OVERLAPPED tx_ovs[2];
    size_t buffer_size;
    int error;
    uint16_t tx_size;
    uint8_t rx_count;
    uint8_t rx_next;
    uint8_t tx_index;
};

static inline void sc_devs_free(void)
{
    if (sc_data.dev_list) {
        free(sc_data.dev_list);
        sc_data.dev_list = NULL;
    }

    if (sc_data.dev_name_indices) {
        free(sc_data.dev_name_indices);
        sc_data.dev_name_indices = NULL;
    }


    sc_data.dev_count = 0;
}

extern int sc_map_cm_error(CONFIGRET cr)
{
    switch (cr) {
    case CR_SUCCESS: return SC_DLL_ERROR_NONE;
    case CR_ACCESS_DENIED: return SC_DLL_ERROR_DEVICE_BUSY;
    case CR_OUT_OF_MEMORY: return SC_DLL_ERROR_OUT_OF_MEM;
    default: return SC_DLL_ERROR_UNKNOWN;
    }
}

static int sc_map_win_error_ex(struct sc_dev_ex* dev, DWORD error)
{
    if (dev && dev->path) {
        WCHAR instance_id[MAX_DEVICE_ID_LEN];
        WCHAR instance_path[MAX_DEVICE_ID_LEN];
        ULONG size = sizeof(instance_id[0]) * (_countof(instance_id) - 1);
        DEVPROPTYPE prop_type = 0;
        CONFIGRET cr = CR_SUCCESS;

        cr = CM_Get_Device_Interface_PropertyW(
            dev->path,
            &DEVPKEY_Device_InstanceId,
            &prop_type,
            (PBYTE)instance_id,
            &size,
            0);

        if (CR_SUCCESS == cr) {
            cr = CM_Get_Device_Interface_ListW(
                (LPGUID)&GUID_DEVINTERFACE_supercan,
                instance_id,
                instance_path,
                _countof(instance_path),
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

            if (CR_SUCCESS == cr) {
                if (instance_path[0] == L'\0') {
                    return SC_DLL_ERROR_DEVICE_GONE;
                }
            }
        }
    }

    switch (error) {
    case ERROR_SUCCESS:
        return SC_DLL_ERROR_NONE;
    case ERROR_FILE_NOT_FOUND:
        return SC_DLL_ERROR_DEVICE_GONE;
    case ERROR_ACCESS_DENIED:
        return SC_DLL_ERROR_DEVICE_BUSY;
    case ERROR_OUTOFMEMORY:
        return SC_DLL_ERROR_OUT_OF_MEM;
    case ERROR_GEN_FAILURE:
    case ERROR_BAD_COMMAND:
        return SC_DLL_ERROR_DEVICE_FAILURE;
    case ERROR_OPERATION_ABORTED:
        return SC_DLL_ERROR_ABORTED;
    default:
        return SC_DLL_ERROR_UNKNOWN;
    }
}

extern int sc_map_win_error(sc_dev_t *_dev, DWORD error)
{
    return sc_map_win_error_ex((struct sc_dev_ex*)_dev, error);
}

SC_DLL_API void sc_version(sc_version_t* version)
{
    version->major = SC_DLL_VERSION_MAJOR;
    version->minor = SC_DLL_VERSION_MINOR;
    version->patch = SC_DLL_VERSION_PATCH;
    version->build = SC_DLL_VERSION_BUILD;
    version->commit = SC_COMMIT;
}

SC_DLL_API void sc_log_set_level(int level)
{
    s_LogLevel = level;
}

SC_DLL_API void sc_log_set_callback(void* ctx, sc_log_callback_t callback)
{
    s_LogCtx = ctx;
    s_LogCallback = callback ? callback : &Nop;
}

SC_DLL_API void sc_init(void)
{

}

SC_DLL_API void sc_uninit(void)
{
    sc_devs_free();
}

SC_DLL_API int sc_dev_scan(void)
{
    CONFIGRET cr;
    int error = SC_DLL_ERROR_NONE;
    ULONG len = 0;

    sc_devs_free();

    //
    // Enumerate all devices exposing the interface. Do this in a loop
    // in case a new interface is discovered while this code is executing,
    // causing CM_Get_Device_Interface_List to return CR_BUFFER_SMALL.
    //
    do {
        cr = CM_Get_Device_Interface_List_SizeW(
            &len,
            (LPGUID)&GUID_DEVINTERFACE_supercan,
            NULL,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        error = sc_map_cm_error(cr);

        if (error) {
            goto Exit;
        }

        sc_data.dev_list = (wchar_t*)malloc(len * sizeof(*sc_data.dev_list));
        if (!sc_data.dev_list) {
            error = SC_DLL_ERROR_OUT_OF_MEM;
            goto Exit;
        }

        cr = CM_Get_Device_Interface_ListW(
            (LPGUID)&GUID_DEVINTERFACE_supercan,
            NULL,
            sc_data.dev_list,
            len,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT);

        if (cr != CR_SUCCESS) {
            if (cr != CR_BUFFER_SMALL) {
                error = sc_map_cm_error(cr);
                goto Exit;
            }
        }
    } while (cr == CR_BUFFER_SMALL);

    assert(!len || sc_data.dev_list);

    sc_data.dev_count = 0;

    for (ULONG i = 0; i < len - 1; ++i) {
        sc_data.dev_count += !sc_data.dev_list[i];
    }

    if (sc_data.dev_count) {
        sc_data.dev_name_indices = malloc(sizeof(*sc_data.dev_name_indices) * sc_data.dev_count);
        if (!sc_data.dev_list) {
            error = SC_DLL_ERROR_OUT_OF_MEM;
            goto Exit;
        }

        size_t last_offset = 0;
        size_t index = 0;
        for (ULONG i = 1; i < len - 1; ++i) {
            if (!sc_data.dev_list[i]) {
                sc_data.dev_name_indices[index++] = last_offset;
                last_offset = i + 1;
            }
        }
    }
Exit:
    return error;
}

SC_DLL_API int sc_dev_count(uint32_t* count)
{
    if (!count) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    *count = (uint32_t)sc_data.dev_count;

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API int sc_dev_id_unicode(uint32_t index, wchar_t* buf, size_t* len)
{
    size_t capacity = 0;
    wchar_t *name = NULL;

    if (!len || index >= sc_data.dev_count) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    capacity = *len;
    name = &sc_data.dev_list[sc_data.dev_name_indices[index]];
    *len = wcslen(name);

    if (capacity >= *len) {
        if (!buf) {
            return SC_DLL_ERROR_INVALID_PARAM;
        }

        memcpy(buf, name, *len * sizeof(*name));
        if (capacity >= *len + 1) {
            buf[*len] = 0;
        }

        return SC_DLL_ERROR_NONE;
    }

    return SC_DLL_ERROR_BUFFER_TOO_SMALL;
}

SC_DLL_API void sc_dev_close(sc_dev_t* dev)
{
    if (dev) {
        struct sc_dev_ex* d = (struct sc_dev_ex*)dev;

        if (d->usb_handle) {
            WinUsb_Free(d->usb_handle);
        }

        if (d->dev_handle && INVALID_HANDLE_VALUE != d->dev_handle) {
            CloseHandle(d->dev_handle);
        }

        free(d->path);
        free(d);
    }
}

SC_DLL_API int sc_dev_open_by_index(uint32_t index, sc_dev_t** dev)
{
    if (!dev || index >= sc_data.dev_count) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    return sc_dev_open_by_id(&sc_data.dev_list[sc_data.dev_name_indices[index]], dev);
}

SC_DLL_API int sc_dev_open_by_id(wchar_t const* id, sc_dev_t** _dev)
{
    int error = SC_DLL_ERROR_NONE;
    DWORD win_error = ERROR_SUCCESS;
    struct sc_dev_ex *dev = NULL;
    USB_DEVICE_DESCRIPTOR deviceDesc;
    PUCHAR cmd_tx_buffer = NULL;
    PUCHAR cmd_rx_buffer = NULL;
    OVERLAPPED cmd_rx_overlapped = { 0 };
    OVERLAPPED cmd_tx_overlapped = { 0 };

    if (!_dev || !id) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    dev = calloc(1, sizeof(*dev));

    if (!dev) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    dev->path = _wcsdup(id);

    if (!dev->path) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    cmd_rx_overlapped.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cmd_rx_overlapped.hEvent) {
        goto error_exit_win_error;
    }

    cmd_tx_overlapped.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cmd_tx_overlapped.hEvent) {
        goto error_exit_win_error;
    }

    dev->dev_handle = CreateFileW(
        id,
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL);

    if (INVALID_HANDLE_VALUE == dev->dev_handle) {
        goto error_exit_win_error;
    }

    if (!WinUsb_Initialize(dev->dev_handle, &dev->usb_handle)) {
        goto error_exit_win_error;
    }

    ULONG transferred;
    if (!WinUsb_GetDescriptor(
        dev->usb_handle,
        USB_DEVICE_DESCRIPTOR_TYPE,
        0,
        0,
        (PBYTE)&deviceDesc,
        sizeof(deviceDesc),
        &transferred)) {
        goto error_exit_win_error;
    }

    if (transferred != sizeof(deviceDesc)) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    USB_INTERFACE_DESCRIPTOR ifaceDescriptor;
    if (!WinUsb_QueryInterfaceSettings(
        dev->usb_handle,
        0,
        &ifaceDescriptor)) {
        goto error_exit_win_error;
    }

    if (ifaceDescriptor.bNumEndpoints < 2) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    // get cmd pipe
    WINUSB_PIPE_INFORMATION pipeInfo;
    if (!WinUsb_QueryPipe(dev->usb_handle,
        0,
        0,
        &pipeInfo)) {
        goto error_exit_win_error;
    }

    if (pipeInfo.PipeType != UsbdPipeTypeBulk) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    dev->exposed.epp_size = pipeInfo.MaximumPacketSize;
    dev->exposed.cmd_buffer_size = pipeInfo.MaximumPacketSize;
    dev->exposed.cmd_epp = ~0x80 & pipeInfo.PipeId;

    if (ifaceDescriptor.bNumEndpoints < 4) {
        error = SC_DLL_ERROR_DEV_NOT_IMPLEMENTED;
        goto Error;
    }

    if (!WinUsb_QueryPipe(
        dev->usb_handle,
        0,
        2,
        &pipeInfo)) {
        goto error_exit_win_error;
    }

    if (pipeInfo.PipeType != UsbdPipeTypeBulk) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    dev->exposed.can_epp = ~0x80 & pipeInfo.PipeId;

    /* Reseting the pipes actually makes the device hang :/ */
    /*if (!WinUsb_ResetPipe(dev->usb_handle, dev->exposed.cmd_epp) ||
        !WinUsb_ResetPipe(dev->usb_handle, dev->exposed.cmd_epp | 0x80) ||
        !WinUsb_ResetPipe(dev->usb_handle, dev->exposed.can_epp) ||
        !WinUsb_ResetPipe(dev->usb_handle, dev->exposed.can_epp | 0x80)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }*/

    BOOL value = TRUE;
    
    // make bulk in pipe raw I/O
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-functions-for-pipe-policy-modification
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.can_epp | 0x80,
        RAW_IO,
        sizeof(value),
        &value)) {
        goto error_exit_win_error;
    }

    cmd_tx_buffer = calloc(dev->exposed.cmd_buffer_size, 1);
    if (!cmd_tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    cmd_rx_buffer = calloc(dev->exposed.cmd_buffer_size, 1);
    if (!cmd_rx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    // terminiate cmd bulk out with ZLP (if required)
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.cmd_epp,
        SHORT_PACKET_TERMINATE,
        sizeof(value),
        &value)) {
        goto error_exit_win_error;
    }

    // make cmd pipe raw
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.cmd_epp | 0x80,
        RAW_IO,
        sizeof(value),
        &value)) {
        goto error_exit_win_error;
    }

    // submit in token
    if (!WinUsb_ReadPipe(
        dev->usb_handle,
        dev->exposed.cmd_epp | 0x80,
        cmd_rx_buffer,
        pipeInfo.MaximumPacketSize,
        NULL,
        &cmd_rx_overlapped) &&
        ERROR_IO_PENDING != GetLastError()) {
        goto error_exit_win_error;
    }

    struct sc_msg_req* hello = (struct sc_msg_req*)cmd_tx_buffer;
    hello->id = SC_MSG_HELLO_DEVICE;
    hello->len = sizeof(*hello);


    if (!WinUsb_WritePipe(
        dev->usb_handle,
        dev->exposed.cmd_epp,
        cmd_tx_buffer,
        hello->len,
        NULL,
        &cmd_tx_overlapped)) {
        if (ERROR_IO_PENDING != GetLastError()) {
            goto error_exit_win_error;
        }
    }

    // wait for a sec for the request to go
    DWORD wait_result = WaitForSingleObject(cmd_tx_overlapped.hEvent, SC_CMD_TIMEOUT_MS);
    switch (wait_result) {
    case WAIT_TIMEOUT:
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    case WAIT_OBJECT_0:
        break;
    default:
        goto error_exit_win_error;
        break;
    }

    if (!WinUsb_GetOverlappedResult(
        dev->usb_handle,
        &cmd_tx_overlapped,
        &transferred,
        FALSE)) {
        goto error_exit_win_error;
    }

    if (transferred != sizeof(*hello)) {
        goto error_exit_win_error;
    }

    // wait for a sec to get the resonse
    wait_result = WaitForSingleObject(cmd_rx_overlapped.hEvent, SC_CMD_TIMEOUT_MS);
    switch (wait_result) {
    case WAIT_TIMEOUT:
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    case WAIT_OBJECT_0:
        break;
    default: {
        goto error_exit_win_error;
    } break;
    }

    if (!WinUsb_GetOverlappedResult(
        dev->usb_handle,
        &cmd_rx_overlapped,
        &transferred,
        FALSE)) {
        goto error_exit_win_error;
    }

    if (sizeof(struct sc_msg_hello) > transferred) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    struct sc_msg_hello *host_hello = (struct sc_msg_hello*)cmd_rx_buffer;
    if (host_hello->id != SC_MSG_HELLO_HOST ||
        host_hello->len < sizeof(*host_hello)) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    if (!host_hello->proto_version
        || host_hello->proto_version > SC_VERSION) {
        error = SC_DLL_ERROR_VERSION_UNSUPPORTED;
        goto Error;
    }

    // network byte order
#if NATIVE_BYTE_ORDER == SC_BYTE_ORDER_LE
    dev->exposed.cmd_buffer_size = Swap16(host_hello->cmd_buffer_size);
#else
    dev->exposed.cmd_buffer_size = host_hello->cmd_buffer_size;
#endif
    if (dev->exposed.cmd_buffer_size < 64) {
        error = SC_DLL_ERROR_VERSION_UNSUPPORTED;
        goto Error;
    }

    if (NATIVE_BYTE_ORDER == host_hello->byte_order) {
        dev->exposed.dev_to_host16 = &Nop16;
        dev->exposed.dev_to_host32 = &Nop32;
    }
    else {
        dev->exposed.dev_to_host16 = &Swap16;
        dev->exposed.dev_to_host32 = &Swap32;
    }

    dev->log_callback = &Nop;
    dev->log_level = SC_DLL_LOG_LEVEL_OFF;

    *_dev = (sc_dev_t*)dev;

Cleanup:
    if (cmd_tx_overlapped.hEvent) {
        CloseHandle(cmd_tx_overlapped.hEvent);
    }

    if (cmd_rx_overlapped.hEvent) {
        CloseHandle(cmd_rx_overlapped.hEvent);
    }

    free(cmd_tx_buffer);
    free(cmd_rx_buffer);

    return error;

error_exit_win_error:
    win_error = GetLastError();
    error = sc_map_win_error_ex(dev, win_error);

Error:
    sc_dev_close((sc_dev_t*)dev);
    goto Cleanup;
}



SC_DLL_API int sc_dev_read(sc_dev_t* _dev, uint8_t pipe, void* _buffer, ULONG bytes, OVERLAPPED* ov)
{
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;
    uint8_t *buffer = _buffer;
    int error = SC_DLL_ERROR_NONE;

    if (!_dev || !ov || !ov->hEvent) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (bytes && !buffer) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (!WinUsb_ReadPipe(
        dev->usb_handle,
        pipe,
        buffer,
        bytes,
        NULL,
        ov)) {
        if (ERROR_IO_PENDING == GetLastError()) {
            error = SC_DLL_ERROR_IO_PENDING;
        } else {
            error = sc_map_win_error_ex(dev, GetLastError());
            goto Exit;
        }
    }

Exit:
    return error;
}

SC_DLL_API int sc_dev_write(sc_dev_t* _dev, uint8_t pipe, void const* _buffer, ULONG bytes, OVERLAPPED* ov)
{
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;
    uint8_t const *buffer = _buffer;
    int error = SC_DLL_ERROR_NONE;


    if (!_dev || !ov || !ov->hEvent) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (bytes && !buffer) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (!WinUsb_WritePipe(
        dev->usb_handle,
        pipe,
        (PUCHAR)buffer,
        bytes,
        NULL,
        ov)) {
        if (ERROR_IO_PENDING == GetLastError()) {
            error = SC_DLL_ERROR_IO_PENDING;
        }
        else {
            error = sc_map_win_error_ex(dev, GetLastError());
            goto Exit;
        }
    }

Exit:
    return error;
}

SC_DLL_API int sc_dev_result(sc_dev_t* _dev, DWORD* transferred, OVERLAPPED* ov, int millis)
{
    int error = SC_DLL_ERROR_NONE;
    DWORD win_error = ERROR_SUCCESS;
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

    if (!_dev || !ov || !ov->hEvent) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (millis < 0) {
get_result:
        if (!WinUsb_GetOverlappedResult(dev->usb_handle, ov, transferred, TRUE)) {
            goto error_exit_win_error;
        }
    }
    else if (0 == millis) {
        if (!WinUsb_GetOverlappedResult(dev->usb_handle, ov, transferred, FALSE)) {
            if (ERROR_IO_INCOMPLETE == GetLastError()) {
                error = SC_DLL_ERROR_IO_PENDING;
            }
            else {
                goto error_exit_win_error;
            }
        }
    }
    else {
        DWORD wait_result = WaitForSingleObject(ov->hEvent, millis);
        switch (wait_result) {
        case WAIT_TIMEOUT:
            error = SC_DLL_ERROR_IO_PENDING;
        case WAIT_OBJECT_0:
            goto get_result;
            break;
        default:
            goto error_exit_win_error;
            break;
        }
    }

Exit:
    return error;

error_exit_win_error:
    win_error = GetLastError();
    error = sc_map_win_error_ex(dev, win_error);
    goto Exit;
}



SC_DLL_API char const* sc_strerror(int error)
{
    switch (error) {
    case SC_DLL_ERROR_UNKNOWN:
        return "unknown error";
    case SC_DLL_ERROR_NONE:
        return "no error";
    case SC_DLL_ERROR_INVALID_PARAM:
        return "invalid parameter";
    case SC_DLL_ERROR_OUT_OF_MEM:
        return "out of memory";
    case SC_DLL_ERROR_DEV_COUNT_CHANGED:
        return "device count changed";
    case SC_DLL_ERROR_DEV_UNSUPPORTED:
        return "unsupported/unknown device";
    case SC_DLL_ERROR_VERSION_UNSUPPORTED:
        return "unsupported " SC_NAME " protocol version";
    case SC_DLL_ERROR_IO_PENDING:
        return "I/O pending";
    case SC_DLL_ERROR_DEVICE_FAILURE:
        return "device failure";
    case SC_DLL_ERROR_DEVICE_BUSY:
        return "device busy";
    case SC_DLL_ERROR_ABORTED:
        return "operation aborted";
    case SC_DLL_ERROR_DEV_NOT_IMPLEMENTED:
        return "feature not implemented";
    case SC_DLL_ERROR_PROTO_VIOLATION:
        return "malformed data buffer";
    case SC_DLL_ERROR_SEQ_VIOLATION:
        return "jumbled CAN message sequence";
    case SC_DLL_ERROR_REASSEMBLY_SPACE:
        return "insufficient message reassembly buffer space";
    case SC_DLL_ERROR_TIMEOUT:
        return "timeout";
    case SC_DLL_ERROR_AGAIN:
        return "try again later";
    case SC_DLL_ERROR_BUFFER_TOO_SMALL:
        return "buffer too small";
    case SC_DLL_ERROR_USER_HANDLE_SIGNALED:
        return "user provided handle was signaled";
    case SC_DLL_ERROR_ACCESS_DENIED:
        return "access denied";
    case SC_DLL_ERROR_INVALID_OPERATION:
        return "invalid operation";
    case SC_DLL_ERROR_DEVICE_GONE:
        return "device gone";
    default:
        return "sc_strerror not implemented";
    }
}

SC_DLL_API int sc_dev_cancel(sc_dev_t *_dev, OVERLAPPED* ov)
{
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

    if (!_dev || !ov || !ov->hEvent) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    if (CancelIoEx(dev->dev_handle, ov)) {
        DWORD transferred;

        if (!WinUsb_GetOverlappedResult(dev->usb_handle, ov, &transferred, TRUE)) {
            DWORD e = GetLastError();
            if (ERROR_OPERATION_ABORTED != e) {
                return sc_map_win_error_ex(dev, e);
            }
        }
    } else {
         DWORD e = GetLastError();

         if (ERROR_NOT_FOUND == e) {
             // all is well wasn' t scheduled
         } else {
             return sc_map_win_error_ex(dev, e);
         }
    }

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API int sc_dev_log_set_level(sc_dev_t* _dev, int level)
{
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

    if (!_dev) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    dev->log_level = level;

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API int sc_dev_log_set_callback(sc_dev_t* _dev, void* ctx, sc_log_callback_t callback)
{
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

    if (!_dev) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    dev->log_ctx = ctx;
    dev->log_callback = callback ? callback : &Nop;

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API void sc_can_stream_uninit(sc_can_stream_t* _stream)
{
    struct sc_stream* stream = (struct sc_stream*)_stream;

    if (stream) {
        struct sc_dev_ex *dev = (struct sc_dev_ex*)stream->dev;

        if (stream->rx_ovs) {
            for (unsigned i = 0; i < stream->rx_count; ++i) {
                if (!stream->rx_ovs[i].hEvent) {
                    break;
                }

                sc_dev_cancel((sc_dev_t*)dev, &stream->rx_ovs[i]);
                CloseHandle(stream->rx_ovs[i].hEvent);
            }

            free(stream->rx_ovs);
        }

        for (size_t i = 0; i < _countof(stream->tx_ovs); ++i) {
            if (!stream->tx_ovs[i].hEvent) {
                break;
            }

            sc_dev_cancel((sc_dev_t*)dev, &stream->tx_ovs[i]);
            CloseHandle(stream->tx_ovs[i].hEvent);
        }

        free(stream->rx_buffers);
        free(stream->tx_buffers);
        free(stream);
    }
}

SC_DLL_API int sc_can_stream_init(
    sc_dev_t* dev,
    DWORD buffer_size,
    void* ctx,
    sc_can_stream_rx_callback callback,
    int rreqs,
    sc_can_stream_t** _stream)
{
    struct sc_stream* stream = NULL;
    int error = SC_DLL_ERROR_NONE;
    DWORD win_error = ERROR_SUCCESS;

    if (!dev || !_stream || !buffer_size || !callback) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (rreqs <= 0) {
        rreqs = SC_CAN_STREAM_DEFAULT_RX_WAIT_HANDLES;
    }

    if (rreqs > SC_CAN_STREAM_MAX_RX_WAIT_HANDLES) {
        error = SC_DLL_ERROR_INVALID_PARAM; // WaitForMultipleObjects limit
        goto Exit;
    }

    stream = calloc(1, sizeof(*stream));
    if (!stream) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Exit;
    }

    stream->dev = (struct sc_dev_ex*)dev;
    stream->ctx = ctx;
    stream->rx_callback = callback;
    stream->buffer_size = buffer_size;
    
    stream->rx_buffers = calloc(rreqs, stream->buffer_size);
    if (!stream->rx_buffers) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->tx_buffers = calloc(2, stream->buffer_size);
    if (!stream->tx_buffers) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->rx_ovs = calloc(rreqs, sizeof(*stream->rx_ovs));
    if (!stream->rx_ovs) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    
    for (size_t i = 0; i < _countof(stream->tx_ovs); ++i) {
        HANDLE h = CreateEventW(NULL, TRUE, TRUE, NULL);

        if (!h) {
            goto error_exit_win_error;
        }

        stream->tx_ovs[i].hEvent = h;
    }

    // create and submit all IN tokens
    for (size_t i = 0; i < (size_t)rreqs; ++i) {
        /* Never, never use automatic reset events!!! 
         *
         * You never know when exactly the event will automatically 
         * reset which would prevent any sort of WaitForMultipleObjects
         * use.
         */
        HANDLE h = CreateEventW(NULL, TRUE, FALSE, NULL);

        if (!h) {
            goto error_exit_win_error;
        }

        stream->rx_ovs[i].hEvent = h;

        if (!WinUsb_ReadPipe(
            stream->dev->usb_handle,
            stream->dev->exposed.can_epp | 0x80,
            stream->rx_buffers + i * stream->buffer_size,
            (ULONG)stream->buffer_size,
            NULL,
            &stream->rx_ovs[i])) {
            DWORD e = GetLastError();

            if (ERROR_IO_PENDING != e) {
                error = sc_map_win_error_ex(stream->dev, e);
                goto Error;
            }

            error = SC_DLL_ERROR_NONE;
        }
        else {
            error = SC_DLL_ERROR_UNKNOWN;
            goto Error;
        }

        LOG_DEV_DEBUG1(stream->dev, "sc_can_stream_init submit %u\n", (unsigned)i);

        ++stream->rx_count;
    }

    stream->exposed.tx_capacity = (uint16_t)stream->buffer_size;

    *_stream = (sc_can_stream_t*)stream;
Exit:
    return error;

error_exit_win_error:
    win_error = GetLastError();
    error = sc_map_win_error_ex((struct sc_dev_ex*)dev, win_error);

Error:
    sc_can_stream_uninit((sc_can_stream_t*)stream);
    goto Exit;
}

static int sc_process_rx_buffer(
    struct sc_stream* stream,
    PUCHAR ptr, uint16_t bytes)
{
    PUCHAR const in_beg = ptr;
    PUCHAR const in_end = in_beg + bytes;
    PUCHAR in_ptr = in_beg;
    int error = SC_DLL_ERROR_NONE;

    while (in_ptr + SC_MSG_CAN_LEN_MULTIPLE <= in_end) {
        struct sc_msg_header const* msg = (struct sc_msg_header const*)in_ptr;

        if (!msg->id || !msg->len) {
            // Allow end-of-input message to work around need to send ZLP
            break;
        }

        if (msg->len < SC_MSG_CAN_LEN_MULTIPLE) {
            error = SC_DLL_ERROR_PROTO_VIOLATION;
            break;
        }

        if (in_ptr + msg->len > in_end) {
            error = SC_DLL_ERROR_PROTO_VIOLATION;
            break;
        }

        error = stream->rx_callback(stream->ctx, msg, msg->len);
        if (error) {
            break;
        }

        in_ptr += msg->len;
    }

    return error;
}

static inline int sc_rx_submit(
    struct sc_stream* stream)
{
    if (!WinUsb_ReadPipe(
        stream->dev->usb_handle,
        stream->dev->exposed.can_epp | 0x80,
        stream->rx_buffers + (size_t)stream->rx_next * stream->buffer_size,
        (ULONG)stream->buffer_size,
        NULL,
        &stream->rx_ovs[stream->rx_next])) {
        DWORD e = GetLastError();

        if (ERROR_IO_PENDING != e) {
            return sc_map_win_error_ex(stream->dev, e);
        }
    }
    else {
        return SC_DLL_ERROR_UNKNOWN;
    }

    stream->rx_next = (stream->rx_next + 1) % stream->rx_count;

    return SC_DLL_ERROR_NONE;
}

static int sc_can_stream_rx_process_signaled_ex(struct sc_stream* stream)
{
    int error = SC_DLL_ERROR_NONE;
    int user_error = SC_DLL_ERROR_NONE;
    DWORD transferred = 0;
    uint8_t const index = stream->rx_next;

    if (!WinUsb_GetOverlappedResult(
        stream->dev->usb_handle,
        &stream->rx_ovs[index],
        &transferred,
        FALSE)) {
        error = sc_map_win_error_ex(stream->dev, GetLastError());
        stream->error = error;
        return error;
    }

    if (transferred) {
        PUCHAR ptr = stream->rx_buffers + stream->buffer_size * index;

        user_error = sc_process_rx_buffer(stream, ptr, (uint16_t)transferred);
    }

    /* Keep stream processing, return user error later on. */
    ResetEvent(stream->rx_ovs[index].hEvent);

    error = sc_rx_submit(stream);
    if (error) {
        stream->error = error;
        return error;
    }

    LOG_DEV_DEBUG3(stream->dev, "sc_can_stream_rx submit %u\n", index);

    if (user_error) {
        return user_error;
    }

    return error;
}

SC_DLL_API int sc_can_stream_rx_process_signaled_wait_handle(sc_can_stream_t* _stream)
{
    struct sc_stream* stream = (struct sc_stream*)_stream;
    
    if (!stream) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    if (stream->error) {
        return stream->error;
    }

    return sc_can_stream_rx_process_signaled_ex(stream);
}

SC_DLL_API int sc_can_stream_rx(sc_can_stream_t* _stream, DWORD timeout_ms)
{
    struct sc_stream* stream = (struct sc_stream*)_stream;
    int error = SC_DLL_ERROR_NONE;
    DWORD dw = 0;

    if (!stream) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    if (stream->error) {
        return stream->error;
    }

    uint8_t const index = stream->rx_next;

    if (stream->exposed.user_handle) {
        HANDLE const wait_handles[] = {
            stream->exposed.user_handle,
            stream->rx_ovs[index].hEvent
        };
        DWORD const wait_count = _countof(wait_handles);

        /* WaitForMultipleObjects returns first signaled handle, 
         * but more _could_ be signaled.
         *
         * https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects
         */
        dw = WaitForMultipleObjects(wait_count, wait_handles, FALSE, timeout_ms);

        switch (dw) {
        case WAIT_OBJECT_0:
            return SC_DLL_ERROR_USER_HANDLE_SIGNALED;
        case WAIT_OBJECT_0 + 1:
            // map to rx event
            dw = WAIT_OBJECT_0;
            break;
        }
    }
    else {
        dw = WaitForSingleObject(stream->rx_ovs[index].hEvent, timeout_ms);
    }
   
    if (WAIT_OBJECT_0 == dw) {
        return sc_can_stream_rx_process_signaled_ex(stream);
    }
    else if (WAIT_TIMEOUT == dw) {
        // nothing to do
        return SC_DLL_ERROR_TIMEOUT;
    }
    
    error = sc_map_win_error_ex(stream->dev, GetLastError());
    stream->error = error;

    return error;
}

SC_DLL_API int sc_can_stream_rx_next_wait_handle(sc_can_stream_t* _stream, HANDLE* handle)
{
    struct sc_stream* stream = (struct sc_stream*)_stream;
    
    if (!stream || !handle) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    *handle = stream->rx_ovs[stream->rx_next].hEvent;

    return SC_DLL_ERROR_NONE;
}

static int sc_can_stream_tx_send_buffer(struct sc_stream* stream)
{
    int error = SC_DLL_ERROR_NONE;
    DWORD dw = 0;
    uint8_t prev_index = (stream->tx_index + 1) & 0x1;

    dw = WaitForSingleObject(stream->tx_ovs[prev_index].hEvent, SC_STREAM_TIMEOUT_MS);
    if (WAIT_OBJECT_0 == dw) {
        ResetEvent(stream->tx_ovs[stream->tx_index].hEvent);
    }
    else if (WAIT_TIMEOUT == dw) {
        LOG_DEV_ERROR(stream->dev, "CAN stream tx timed out after %u [ms]\n", SC_STREAM_TIMEOUT_MS);
        error = SC_DLL_ERROR_TIMEOUT;
        goto exit_error;
    } 
    else {
        goto fetch_error;
    }

    if (WinUsb_WritePipe(
        stream->dev->usb_handle,
        stream->dev->exposed.can_epp,
        &stream->tx_buffers[stream->tx_index * stream->buffer_size],
        stream->tx_size,
        NULL,
        &stream->tx_ovs[stream->tx_index])) {
        error = SC_DLL_ERROR_UNKNOWN; // shouldn't happen with OVERLAPPED
        goto exit_error;
    }

    dw = GetLastError();
    if (ERROR_IO_PENDING != dw) {
        goto fetch_error;
    }

    // swap tx buffers
    stream->tx_index = prev_index;

exit:
    return error;

fetch_error:
    dw = GetLastError();
    error = sc_map_win_error_ex(stream->dev, dw);

exit_error:
    stream->error = error;
    goto exit;
}


SC_DLL_API int sc_can_stream_tx_batch_begin(sc_can_stream_t* _stream)
{
    struct sc_stream* stream = (struct sc_stream*)_stream;
    
    if (!stream) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    if (stream->tx_size) {
        return SC_DLL_ERROR_INVALID_OPERATION;
    }

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API int sc_can_stream_tx_batch_add(
    sc_can_stream_t* _stream,
    uint8_t const** buffers,
    uint16_t const* sizes,
    size_t count,
    size_t* added)
{
    struct sc_stream* stream = (struct sc_stream*)_stream;
    size_t buffers_to_add = 0;
    uint16_t bytes = 0;

    if (!stream || !buffers || !added) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    bytes = stream->tx_size;

    for (; buffers_to_add < count; ++buffers_to_add) {
        uint16_t new_bytes = bytes + sizes[buffers_to_add];
        if (new_bytes > stream->exposed.tx_capacity) {
            break;
        }

        bytes = new_bytes;
    }

    for (size_t i = 0; i < buffers_to_add; ++i) {
        memcpy(&stream->tx_buffers[stream->tx_index * stream->buffer_size + stream->tx_size], buffers[i], sizes[i]);
        stream->tx_size += sizes[i];
    }

    *added = buffers_to_add;

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API int sc_can_stream_tx_batch_end(sc_can_stream_t* _stream)
{
    struct sc_stream* stream = (struct sc_stream*)_stream;

    if (!stream) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    if (stream->error) {
        return stream->error;
    }

    if (stream->tx_size) {
        if (stream->tx_size & (SC_MSG_CAN_LEN_MULTIPLE - 1)) {
            return SC_DLL_ERROR_PROTO_VIOLATION;
        }

        // Check if we need to work around having to send a ZLP
        if (stream->dev->exposed.epp_size < stream->buffer_size &&
            stream->tx_size < stream->buffer_size &&
            (stream->tx_size % stream->dev->exposed.epp_size) == 0) {
            *((uint32_t*)&stream->tx_buffers[stream->tx_index * stream->buffer_size + stream->tx_size]) = 0;
            stream->tx_size += 4;
        }

        int error = sc_can_stream_tx_send_buffer(stream);
        if (error) {
            return error;
        }

        stream->tx_size = 0;
    }

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API int sc_can_stream_tx(
    sc_can_stream_t* stream,
    uint8_t const* ptr,
    uint16_t bytes)
{
    size_t added = 0;
    int error = 0;


    error = sc_can_stream_tx_batch_begin(stream);

    if (error) {
        return error;
    }

    error = sc_can_stream_tx_batch_add(stream, &ptr, &bytes, 1, &added);
    if (error) {
        return error;
    }

    if (!added) {
        return SC_DLL_ERROR_INVALID_PARAM; // bytes to write likely exceed max. batch size
    }

    error = sc_can_stream_tx_batch_end(stream);

    return error;
}




SC_DLL_API void sc_cmd_ctx_uninit(sc_cmd_ctx_t* ctx)
{
    if (ctx) {
        if (ctx->rx_ov.hEvent) {
            CloseHandle(ctx->rx_ov.hEvent);
            ctx->rx_ov.hEvent = NULL;
        }

        if (ctx->tx_ov.hEvent) {
            CloseHandle(ctx->tx_ov.hEvent);
            ctx->tx_ov.hEvent = NULL;
        }

        if (ctx->tx_buffer) {
            free(ctx->tx_buffer);
            ctx->tx_buffer = NULL;
        }

        ctx->rx_buffer = NULL;
    }
}

SC_DLL_API int sc_cmd_ctx_init(sc_cmd_ctx_t* ctx, sc_dev_t* dev)
{
    int error = SC_DLL_ERROR_NONE;

    if (!ctx || !dev) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto error_exit;
    }

    memset(ctx, 0, sizeof(*ctx));

    ctx->dev = dev;

    ctx->tx_ov.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ctx->tx_ov.hEvent) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto error_exit;
    }

    ctx->rx_ov.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!ctx->rx_ov.hEvent) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto error_exit;
    }

    ctx->tx_buffer = malloc(2 * (size_t)ctx->dev->cmd_buffer_size);
    if (!ctx->tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto error_exit;
    }

    ctx->rx_buffer = ctx->tx_buffer + ctx->dev->cmd_buffer_size;

success_exit:
    return error;

error_exit:
    sc_cmd_ctx_uninit(ctx);
    goto success_exit;
}

SC_DLL_API int sc_cmd_ctx_run(
    sc_cmd_ctx_t* ctx,
    uint16_t bytes,
    uint16_t* response_size,
    DWORD timeout_ms)
{
    struct sc_dev_ex* dev = NULL;
    DWORD transferred = 0;
    DWORD result = 0;
    int error = SC_DLL_ERROR_NONE;
    uint16_t dummy = 0;
    bool rx_submitted = false;

    if (!ctx || !bytes) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto error_exit;
    }

    if (!response_size) {
        response_size = &dummy;
    }

    *response_size = 0;

    dev = (struct sc_dev_ex*)ctx->dev;

    // submit in token
    if (!WinUsb_ReadPipe(
        dev->usb_handle,
        dev->exposed.cmd_epp | 0x80,
        ctx->rx_buffer,
        dev->exposed.cmd_buffer_size,
        NULL,
        &ctx->rx_ov)) {
        DWORD e = GetLastError();
        if (ERROR_IO_PENDING != e) {
            error = sc_map_win_error_ex(dev, e);
            goto error_exit;
        }
    }

    rx_submitted = true;

    if (!WinUsb_WritePipe(
        dev->usb_handle,
        dev->exposed.cmd_epp,
        ctx->tx_buffer,
        bytes,
        NULL,
        &ctx->tx_ov)) {
        DWORD e = GetLastError();
        if (ERROR_IO_PENDING != e) {
            error = sc_map_win_error_ex(dev, e);
            goto error_exit;
        }
    }

    result = WaitForSingleObject(ctx->tx_ov.hEvent, timeout_ms);
    switch (result) {
    case WAIT_OBJECT_0:
        break;
    case WAIT_TIMEOUT:
        error = SC_DLL_ERROR_TIMEOUT;
        sc_dev_cancel((sc_dev_t*)dev, &ctx->tx_ov);
        goto error_exit;
    default:
        goto error_exit_win_error;
        break;
    }

    if (!WinUsb_GetOverlappedResult(dev->usb_handle, &ctx->tx_ov, &transferred, FALSE)) {
        goto error_exit_win_error;
    }

    if (transferred < bytes) {
        error = SC_DLL_ERROR_DEVICE_FAILURE;
        goto error_exit;
    }

    result = WaitForSingleObject(ctx->rx_ov.hEvent, timeout_ms);
    switch (result) {
    case WAIT_OBJECT_0:
        break;
    case WAIT_TIMEOUT:
        error = SC_DLL_ERROR_TIMEOUT;
        goto error_exit;
    default:
        goto error_exit_win_error;
        break;
    }

    rx_submitted = false;

    if (!WinUsb_GetOverlappedResult(dev->usb_handle, &ctx->rx_ov, &transferred, FALSE)) {
        goto error_exit_win_error;
    }

    *response_size = (uint16_t)transferred;

success_exit:
    return error;

error_exit_win_error:
    error = sc_map_win_error_ex(dev, GetLastError());

error_exit:
    if (rx_submitted) {
        sc_dev_cancel((sc_dev_t*)dev, &ctx->rx_ov);
    }
    goto success_exit;
}

