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
#include <supercan_dll.h>
#include <supercan_winapi.h>
#include <stdio.h>


#define CHUNKY_CHUNK_SIZE_TYPE uint16_t
#define CHUNKY_BUFFER_SIZE_TYPE uint16_t
//#define CHUNKY_CHUNK_SIZE USB_TRANSFER_SIZE
#define CHUNKY_BYTESWAP
#include "chunky.h"
#undef CHUNKY_CHUNK_SIZE_TYPE
#undef CHUNKY_BUFFER_SIZE_TYPE
//#undef CHUNKY_CHUNK_SIZE

// I am going to assume Windows on ARM is little endian
#define NATIVE_BYTE_ORDER SC_BYTE_ORDER_LE


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

static uint16_t dev_swap16(void* ctx, uint16_t value)
{
    return ((sc_dev_t*)ctx)->dev_to_host16(value);
}

struct sc_dev_ex {
    sc_dev_t exposed;
    HANDLE dev_handle;
    WINUSB_INTERFACE_HANDLE usb_handle;
};

struct sc_buffer_seq {
    uint16_t seq_no;
    uint8_t index;
    uint8_t seq_count;
};

struct sc_stream {
    struct sc_dev_ex* dev;
    void* ctx;
    sc_can_stream_rx_callback rx_callback;
    PUCHAR rx_buffers;
    PUCHAR tx_buffer;
    HANDLE *rx_events;
    HANDLE* tx_events;
    OVERLAPPED* rx_ovs;    
    uint8_t* rx_map;
    uint8_t* rx_reassembly_buffer;
    struct sc_buffer_seq* rx_parked;
    OVERLAPPED tx_ov;
    chunky_reader r;
    chunky_writer w;
    size_t buffer_size;
    uint16_t rx_reassembly_count;
    uint16_t rx_reassembly_capacity;
    uint8_t rx_count;
    uint8_t rx_submit_count;
    uint8_t rx_park_count;
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

static inline int HrToError(HRESULT hr)
{
    if (SUCCEEDED(hr)) {
        return SC_DLL_ERROR_NONE;
    }


    switch (hr) {
    case E_OUTOFMEMORY:
        return SC_DLL_ERROR_OUT_OF_MEM;
    case E_ACCESSDENIED:
        return SC_DLL_ERROR_DEVICE_BUSY;
    case 0x8007001f: // ERROR_GEN_FAILURE, 31 (0x1F), A device attached to the system is not functioning.
    case 0x80070016: // 		hr	HRESULT_FROM_WIN32(ERROR_BAD_COMMAND) : The device does not recognize the command. 	HRESULT
        return SC_DLL_ERROR_DEVICE_FAILURE;
    case 0x800703E3: // ERROR_OPERATION_ABORTED, 995 (0x3E3), The I/O operation has been aborted because of either a thread exit or an application request.
        return SC_DLL_ERROR_ABORTED;
    default:
        return SC_DLL_ERROR_UNKNOWN;
    }
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

        if (cr != CR_SUCCESS) {
            HRESULT hr = HRESULT_FROM_WIN32(CM_MapCrToWin32Err(cr, ERROR_INVALID_DATA));
            error = HrToError(hr);
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
                HRESULT hr = HRESULT_FROM_WIN32(CM_MapCrToWin32Err(cr, ERROR_INVALID_DATA));
                error = HrToError(hr);
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

        free(d);
    }
}

SC_DLL_API int sc_dev_open(uint32_t index, sc_dev_t **_dev)
{
    int error = SC_DLL_ERROR_NONE;
    struct sc_dev_ex *dev = NULL;
    USB_DEVICE_DESCRIPTOR deviceDesc;
    PUCHAR cmd_tx_buffer = NULL;
    PUCHAR cmd_rx_buffer = NULL;
    OVERLAPPED cmd_rx_overlapped = { 0 };
    OVERLAPPED cmd_tx_overlapped = { 0 };
    

    if (!_dev || index >= sc_data.dev_count) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Error;
    }

    dev = calloc(1, sizeof(*dev));
    if (!dev) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    cmd_rx_overlapped.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cmd_rx_overlapped.hEvent) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    cmd_tx_overlapped.hEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!cmd_tx_overlapped.hEvent) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    dev->dev_handle = CreateFile(
        &sc_data.dev_list[sc_data.dev_name_indices[index]],
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
        NULL);

    if (INVALID_HANDLE_VALUE == dev->dev_handle) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    if (!WinUsb_Initialize(dev->dev_handle, &dev->usb_handle)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    if (pipeInfo.PipeType != UsbdPipeTypeBulk) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    dev->exposed.can_epp = ~0x80 & pipeInfo.PipeId;

    BOOL value = TRUE;
    // Settings this flag makes Windows send an extra zlp
    // if the buffer size is a multiple of the endpoint size 
    // AND the device can receive more data than the size of the endpoint.
    // 
    // This is here to not send full 512 bytes buffers on a 64 endpoint
    // but only as much as required.
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.can_epp,
        SHORT_PACKET_TERMINATE,
        sizeof(value),
        &value)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    // make bulk in pipe raw i/o
    // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-functions-for-pipe-policy-modification
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.can_epp | 0x80,
        RAW_IO,
        sizeof(value),
        &value)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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

    // make cmd pipe raw
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.cmd_epp,
        SHORT_PACKET_TERMINATE,
        sizeof(value),
        &value)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    // make cmd pipe raw
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.cmd_epp | 0x80,
        RAW_IO,
        sizeof(value),
        &value)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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
            DWORD e = GetLastError();
            HRESULT hr = HRESULT_FROM_WIN32(e);
            error = HrToError(hr);
            goto Error;
        }
    }

    // wait for a sec for the request to go
    DWORD wait_result = WaitForSingleObject(cmd_tx_overlapped.hEvent, 1000);
    switch (wait_result) {
    case WAIT_TIMEOUT:
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    case WAIT_OBJECT_0:
        break;
    default: {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    } break;
    }

    if (!WinUsb_GetOverlappedResult(
        dev->usb_handle,
        &cmd_tx_overlapped,
        &transferred,
        FALSE)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    if (transferred != sizeof(*hello)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    // wait for a sec to get the resonse
    wait_result = WaitForSingleObject(cmd_rx_overlapped.hEvent, 1000);
    switch (wait_result) {
    case WAIT_TIMEOUT:
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    case WAIT_OBJECT_0:
        break;
    default: {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    } break;
    }
    
    if (!WinUsb_GetOverlappedResult(
        dev->usb_handle,
        &cmd_rx_overlapped,
        &transferred,
        FALSE)) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
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

Error:
    sc_dev_close((sc_dev_t*)dev);
    goto Cleanup;
}



SC_DLL_API int sc_dev_read(sc_dev_t* _dev, uint8_t pipe, uint8_t* buffer, ULONG bytes, OVERLAPPED* ov)
{
    int error = SC_DLL_ERROR_NONE;
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

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
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            error = HrToError(hr);
            goto Exit;
        }
    }

Exit:
    return error;
}

SC_DLL_API int sc_dev_write(sc_dev_t* _dev, uint8_t pipe, uint8_t const* buffer, ULONG bytes, OVERLAPPED* ov)
{
    int error = SC_DLL_ERROR_NONE;
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

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
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            error = HrToError(hr);
            goto Exit;
        }
    }

Exit:
    return error;
}

SC_DLL_API int sc_dev_result(sc_dev_t* _dev, DWORD* transferred, OVERLAPPED* ov, int millis)
{
    int error = SC_DLL_ERROR_NONE;
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

    if (!_dev || !ov || !ov->hEvent) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (millis < 0) {
get_result:
        if (!WinUsb_GetOverlappedResult(dev->usb_handle, ov, transferred, TRUE)) {
            DWORD e = GetLastError();
            HRESULT hr = HRESULT_FROM_WIN32(e);
            error = HrToError(hr);
        }
    }
    else if (0 == millis) {
        if (!WinUsb_GetOverlappedResult(dev->usb_handle, ov, transferred, FALSE)) {
            if (ERROR_IO_INCOMPLETE == GetLastError()) {
                error = SC_DLL_ERROR_IO_PENDING;
            }
            else {
                DWORD e = GetLastError();
                HRESULT hr = HRESULT_FROM_WIN32(e);
                error = HrToError(hr);
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
        default: {
            DWORD e = GetLastError();
            HRESULT hr = HRESULT_FROM_WIN32(e);
            error = HrToError(hr);
        } break;
        }
    }
Exit:
    return error;
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
    default:
        return "sc_strerror not implemented";
    }
}

SC_DLL_API int sc_dev_cancel(sc_dev_t *_dev, OVERLAPPED* ov)
{
    int error = SC_DLL_ERROR_NONE;
    struct sc_dev_ex* dev = (struct sc_dev_ex*)_dev;

    if (!_dev || !ov || !ov->hEvent) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (CancelIoEx(dev->dev_handle, ov)) {
        DWORD transferred;
        if (!WinUsb_GetOverlappedResult(dev->usb_handle, ov, &transferred, TRUE)) {
            DWORD e = GetLastError();
            if (ERROR_OPERATION_ABORTED != e) {
                HRESULT hr = HRESULT_FROM_WIN32(e);
                error = HrToError(hr);
                goto Exit;
            }
        }
    } else {
         DWORD e = GetLastError();
         if (ERROR_NOT_FOUND == e) {
             // all is well wasn' t scheduled
         } else {
             HRESULT hr = HRESULT_FROM_WIN32(e);
             error = HrToError(hr);
             goto Exit;
         }
    }


Exit:
    return error;
}

SC_DLL_API void sc_can_stream_uninit(sc_can_stream_t _stream)
{
    struct sc_stream* stream = _stream;

    if (stream) {
        if (stream->rx_ovs) {
            for (unsigned i = 0; i < stream->rx_count; ++i) {
                if (!stream->rx_ovs[i].hEvent) {
                    break;
                }
                CloseHandle(stream->rx_ovs[i].hEvent);
            }

            free(stream->rx_ovs);
        }

        if (stream->tx_ov.hEvent) {
            CloseHandle(stream->tx_ov.hEvent);
        }

        free(stream->rx_buffers);
        free(stream->tx_buffer);
        free(stream->rx_events);
        free(stream->tx_events);
        free(stream->rx_map);
        free(stream->rx_reassembly_buffer);
        free(stream->rx_parked);
        free(stream);
    }
}

SC_DLL_API int sc_can_stream_init(
    sc_dev_t* dev, 
    DWORD buffer_size,
    void* ctx,
    sc_can_stream_rx_callback callback,
    int rreqs, 
    sc_can_stream_t* _stream)
{
    struct sc_stream* stream = NULL;
    int error = SC_DLL_ERROR_NONE;

    if (!dev || !_stream || !buffer_size || !callback) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    if (rreqs <= 0) {
        rreqs = 8;
    }

    if (rreqs > 64) {
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
    stream->rx_count = rreqs;
    stream->rx_reassembly_capacity = 256;
    chunky_reader_init(&stream->r, dev, &dev_swap16);
    chunky_writer_init(&stream->w, dev->epp_size, dev, &dev_swap16);

    stream->rx_buffers = calloc(rreqs, buffer_size);
    if (!stream->rx_buffers) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->tx_buffer = malloc(stream->buffer_size);
    if (!stream->tx_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->rx_ovs = calloc(rreqs, sizeof(*stream->rx_ovs));
    if (!stream->rx_ovs) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->rx_map = calloc(rreqs, sizeof(*stream->rx_map));
    if (!stream->rx_map) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->rx_events = calloc(rreqs, sizeof(*stream->rx_events));
    if (!stream->rx_events) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->rx_reassembly_buffer = calloc(stream->rx_reassembly_capacity, sizeof(*stream->rx_reassembly_buffer));
    if (!stream->rx_reassembly_buffer) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    stream->rx_parked = calloc(stream->rx_count, sizeof(*stream->rx_parked));
    if (!stream->rx_parked) {
        error = SC_DLL_ERROR_OUT_OF_MEM;
        goto Error;
    }

    for (unsigned i = 0; i < (unsigned)rreqs; ++i) {
        HANDLE h = CreateEventW(NULL, FALSE, FALSE, NULL);
        if (!h) {
            DWORD e = GetLastError();
            HRESULT hr = HRESULT_FROM_WIN32(e);
            error = HrToError(hr);
            goto Error;
        }

        stream->rx_ovs[i].hEvent = h;
        stream->rx_events[i] = h;
        stream->rx_map[i] = i;
    }

    stream->tx_ov.hEvent = CreateEventW(NULL, TRUE, TRUE, NULL);;
    if (!stream->tx_ov.hEvent) {
        DWORD e = GetLastError();
        HRESULT hr = HRESULT_FROM_WIN32(e);
        error = HrToError(hr);
        goto Error;
    }

    // submit all reads tokens
    for (size_t i = 0; i < (size_t)rreqs; ++i) {
        if (!WinUsb_ReadPipe(
            stream->dev->usb_handle,
            stream->dev->exposed.can_epp | 0x80,
            stream->rx_buffers + i * stream->buffer_size,
            stream->buffer_size,
            NULL,
            &stream->rx_ovs[i])) {
            DWORD e = GetLastError();
            if (ERROR_IO_PENDING != e) {
                HRESULT hr = HRESULT_FROM_WIN32(e);
                error = HrToError(hr);
                goto Error;
            }

            error = SC_DLL_ERROR_NONE;
        }
        else {
            error = SC_DLL_ERROR_UNKNOWN;
            goto Error;
        }
    }

    stream->rx_submit_count = stream->rx_count;
    chunky_writer_set(&stream->w, stream->tx_buffer, (uint16_t)stream->buffer_size);
    
    *_stream = stream;
Exit: 
    return error;

Error:
    sc_can_stream_uninit(stream);
    goto Exit;
}

static int sc_process_rx_buffer(
    struct sc_stream* stream,
    PUCHAR ptr, uint16_t bytes,
    uint16_t* left)
{
    PUCHAR in_beg = ptr;
    PUCHAR in_end = in_beg + bytes;
    PUCHAR in_ptr = in_beg;
    int error = SC_DLL_ERROR_NONE;

    *left = 0;

    while (in_ptr + SC_MSG_HEADER_LEN <= in_end) {
        struct sc_msg_header const* msg = (struct sc_msg_header const*)in_ptr;
        if (msg->len < SC_MSG_HEADER_LEN) {
            return SC_DLL_ERROR_PROTO_VIOLATION;
        }

        if (in_ptr + msg->len > in_end) {
            *left = (uint16_t)(in_end - in_ptr);
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
    struct sc_stream* stream,
    uint8_t index)

{
    if (!WinUsb_ReadPipe(
        stream->dev->usb_handle,
        stream->dev->exposed.can_epp | 0x80,
        stream->rx_buffers + (size_t)index * stream->buffer_size,
        (ULONG)stream->buffer_size,
        NULL,
        &stream->rx_ovs[index])) {
        DWORD e = GetLastError();
        if (ERROR_IO_PENDING != e) {
            HRESULT hr = HRESULT_FROM_WIN32(e);
            return HrToError(hr);
        }
    }
    else {
        return SC_DLL_ERROR_UNKNOWN;
    }

    stream->rx_map[stream->rx_submit_count] = index;
    stream->rx_events[stream->rx_submit_count] = stream->rx_ovs[index].hEvent;
    ++stream->rx_submit_count;

    return SC_DLL_ERROR_NONE;
}

static int sc_process_chunks(
    struct sc_stream* stream,
    unsigned index,
    unsigned seq_count)
{
    uint8_t* base_ptr = stream->rx_buffers + (size_t)stream->buffer_size * index;

    for (unsigned i = 0; i < seq_count; ++i) {
        PUCHAR in_beg = base_ptr + i * stream->dev->exposed.epp_size;
        PUCHAR in_end = in_beg + stream->dev->exposed.epp_size;
        PUCHAR in_ptr = NULL;
        uint16_t data_size = 0;
        int error = chunky_reader_chunk_process(&stream->r, in_beg, &in_ptr, &data_size);

        if (CHUNKYE_NONE != error) {
            //fprintf(stderr, "chunk extraction failed seq=%u count=%u index=%u\n", (unsigned)seq_no, seq_count, (unsigned)index);
            return SC_DLL_ERROR_SEQ_VIOLATION;
        }

        assert(in_ptr);
        if (!data_size || data_size >= stream->dev->exposed.epp_size) {
            return SC_DLL_ERROR_PROTO_VIOLATION;
        }

        in_beg = in_ptr;
        in_end = in_ptr + data_size;

        if (stream->rx_reassembly_count) {
            if (stream->rx_reassembly_capacity < (size_t)stream->rx_reassembly_count + data_size) {
                return SC_DLL_ERROR_REASSEMBLY_SPACE;
            }

            memcpy(&stream->rx_reassembly_buffer[stream->rx_reassembly_count], in_ptr, data_size);

            in_beg = stream->rx_reassembly_buffer;
            in_end = in_beg + data_size + stream->rx_reassembly_count;
            in_ptr = in_beg;

            stream->rx_reassembly_count = 0;
        }

        uint16_t left = 0;
        error = sc_process_rx_buffer(stream, in_ptr, (uint16_t)(in_end - in_ptr), &left);
        if (error) {
            return error;
        }

        if (left) {
            //fprintf(stderr, "save %u bytes\n", left);
            if (in_ptr == stream->rx_reassembly_buffer) {
                memmove(stream->rx_reassembly_buffer, in_end - left, left);
            }
            else {
                memcpy(stream->rx_reassembly_buffer, in_end - left, left);
            }
        }

        stream->rx_reassembly_count = left;
    }

    return SC_DLL_ERROR_NONE;
}

SC_DLL_API int sc_can_stream_run(
    sc_can_stream_t _stream,
    DWORD timeout_ms)
{
    struct sc_stream* stream = _stream;
    int error = SC_DLL_ERROR_NONE;

    if (!stream) {
        error = SC_DLL_ERROR_INVALID_PARAM;
        goto Exit;
    }

    for (bool done = false; !done; ) {
        done = true;

        uint16_t target_seq_no = stream->r.seq_no + 1;
        for (unsigned i = 0; i < stream->rx_park_count; ++i) {
            struct sc_buffer_seq const* e = &stream->rx_parked[i];
            if (target_seq_no != e->seq_no) {
                continue;
            }

            uint8_t index = e->index;
            uint8_t seq_count = e->seq_count;
            stream->rx_parked[i] = stream->rx_parked[stream->rx_park_count - 1];
            --stream->rx_park_count;
            fprintf(stderr, "restore seq=%u count=%u index=%u\n", target_seq_no, seq_count, (unsigned)index);

            int error = sc_process_chunks(stream, index, seq_count);
            if (error) {
                goto Exit;
            }

            error = sc_rx_submit(stream, index);
            if (error) {
                goto Exit;
            }

            done = false;
        }
    }

    if (stream->rx_submit_count) {
        DWORD result = WaitForMultipleObjects(stream->rx_submit_count, stream->rx_events, FALSE, timeout_ms);
        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + stream->rx_submit_count) {
            uint8_t submit_index = (uint8_t)(result - WAIT_OBJECT_0);
            uint8_t index = stream->rx_map[submit_index];
            assert(index < stream->rx_count);
            uint8_t replacement = stream->rx_map[stream->rx_submit_count - 1];
            assert(replacement < stream->rx_count);
            stream->rx_map[submit_index] = replacement;
            stream->rx_events[submit_index] = stream->rx_ovs[replacement].hEvent;
            --stream->rx_submit_count;


            DWORD transferred = 0;
            if (!WinUsb_GetOverlappedResult(
                stream->dev->usb_handle,
                &stream->rx_ovs[index],
                &transferred,
                FALSE)) {

                DWORD e = GetLastError();
                HRESULT hr = HRESULT_FROM_WIN32(e);
                error = HrToError(hr);
                goto Exit;
            }

            if (0 == transferred) {
                goto resubmit;
            }

            uint16_t seq_count = (uint16_t)(transferred / stream->dev->exposed.epp_size);
            if (!seq_count || (DWORD)seq_count * stream->dev->exposed.epp_size != transferred) {
                error = SC_DLL_ERROR_PROTO_VIOLATION;
                goto Exit;
            }

            assert(seq_count < 256);

            // process buffer
            uint8_t *base_ptr = stream->rx_buffers + (size_t)stream->buffer_size * index;
            uint16_t buffer_seq_no;
            uint16_t target_seq_no = stream->r.seq_no + 1; // must be in type to lap
            {
                struct chunky_chunk_hdr* hdr = (struct chunky_chunk_hdr*)base_ptr;
                buffer_seq_no = stream->dev->exposed.dev_to_host16(hdr->seq_no);
            }

            if (target_seq_no != buffer_seq_no) {
                if (stream->rx_park_count == stream->rx_count) {
                    error = SC_DLL_ERROR_UNKNOWN;
                    goto Exit;
                }

                
                struct sc_buffer_seq* e = &stream->rx_parked[stream->rx_park_count++];
                e->index = index;
                e->seq_no = buffer_seq_no;
                e->seq_count = (uint8_t)seq_count;

                fprintf(stderr, "park seq=%u count=%u index=%u\n", (unsigned)buffer_seq_no, seq_count, (unsigned)index);
            }
            else {
                error = sc_process_chunks(stream, index, (uint8_t)seq_count);
                if (error) {
                    goto Exit;
                }

            resubmit:
                error = sc_rx_submit(stream, index);
                if (error) {
                    goto Exit;
                }
            }
        }
        else if (WAIT_TIMEOUT == result) {
            // nothing to do
            error = SC_DLL_ERROR_TIMEOUT;
        }
        else {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            error = HrToError(hr);    
            goto Exit;
        }
    }

    

Exit:
    return error;
}

SC_DLL_API int sc_can_stream_tx(
    sc_can_stream_t _stream,
    void const* _ptr,
    size_t bytes,
    DWORD timeout_ms,
    size_t* written)
{
    struct sc_stream* stream = _stream;
    uint8_t const* ptr = _ptr;
    uint8_t const* end = ptr + bytes;
    size_t dummy = 0;
    int error = SC_DLL_ERROR_NONE;
    DWORD result = 0;

    if (!stream || !ptr || !bytes) {
        return SC_DLL_ERROR_INVALID_PARAM;
    }

    if (!written) {
        written = &dummy;
    }

    *written = 0;

    while (ptr + SC_MSG_HEADER_LEN <= end) {
        struct sc_msg_header const* msg = (struct sc_msg_header const*)ptr;

        if (!msg->id || !msg->len) {
            break;
        }

        if (msg->len < SC_MSG_HEADER_LEN) {
            error = SC_DLL_ERROR_PROTO_VIOLATION;
            goto Exit;
        }

        if (ptr + msg->len > end) {
            error = SC_DLL_ERROR_INVALID_PARAM;
            goto Exit;
        }

        unsigned message_bytes = msg->len;
put:    
        if (chunky_writer_available(&stream->w)) {
            unsigned w = chunky_writer_write(&stream->w, ptr, message_bytes);
            ptr += w;
            message_bytes -= w;
            if (message_bytes) { // chunk full
                unsigned usb_data_size = chunky_writer_finalize(&stream->w);

                ResetEvent(stream->tx_ov.hEvent);

                if (WinUsb_WritePipe(
                    stream->dev->usb_handle,
                    stream->dev->exposed.can_epp,
                    stream->tx_buffer,
                    usb_data_size,
                    NULL,
                    &stream->tx_ov)) {
                    error = SC_DLL_ERROR_UNKNOWN; // shouldn't happen with OVERLAPPED
                    goto Exit;
                }
                else {
                    DWORD e = GetLastError();
                    if (ERROR_IO_PENDING != e) {
                        HRESULT hr = HRESULT_FROM_WIN32(e);
                        error = HrToError(hr);
                        goto Exit;
                    }
                }

                chunky_writer_set(&stream->w, stream->tx_buffer, (uint16_t)stream->buffer_size);
            }
            else {
                *written += msg->len;
                continue;
            }
        }        

        result = WaitForSingleObject(stream->tx_ov.hEvent, timeout_ms);
        if (WAIT_OBJECT_0 == result) {
            goto put;
        }
        else if (WAIT_TIMEOUT == result) {
            error = SC_DLL_ERROR_TIMEOUT;
            goto Exit;
        }
        else {
            DWORD e = GetLastError();
            HRESULT hr = HRESULT_FROM_WIN32(e);
            error = HrToError(hr);
            goto Exit;
        }
    }

    if (chunky_writer_any(&stream->w)) {
        result = WaitForSingleObject(stream->tx_ov.hEvent, timeout_ms);
        if (WAIT_OBJECT_0 == result) {
            unsigned usb_data_size = chunky_writer_finalize(&stream->w);

            ResetEvent(stream->tx_ov.hEvent);

            if (WinUsb_WritePipe(
                stream->dev->usb_handle,
                stream->dev->exposed.can_epp,
                stream->tx_buffer,
                usb_data_size,
                NULL,
                &stream->tx_ov)) {
                error = SC_DLL_ERROR_UNKNOWN; // shouldn't happen with OVERLAPPED
            }
            else {
                DWORD e = GetLastError();
                if (ERROR_IO_PENDING != e) {
                    HRESULT hr = HRESULT_FROM_WIN32(e);
                    error = HrToError(hr);
                }
            }

            chunky_writer_set(&stream->w, stream->tx_buffer, (uint16_t)stream->buffer_size);
        }
        else if (WAIT_TIMEOUT == result) {
            error = SC_DLL_ERROR_TIMEOUT;
        }
        else {
            DWORD e = GetLastError();
            HRESULT hr = HRESULT_FROM_WIN32(e);
            error = HrToError(hr);
        }
    }

Exit:
    return error;
}