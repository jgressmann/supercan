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

#include "pch.h"

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
    case 0x8007001f:
    case 0x80070016: // 		hr	HRESULT_FROM_WIN32(ERROR_BAD_COMMAND) : The device does not recognize the command. 	HRESULT
        return SC_DLL_ERROR_DEVICE_FAILURE;
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

        if (d->exposed.msg_pipe_ptr) {
            free(d->exposed.msg_pipe_ptr);
        }

        free(d);
    }
}

SC_DLL_API int sc_dev_open(uint32_t index, sc_dev_t **_dev)
{
    int error = SC_DLL_ERROR_NONE;
    struct sc_dev_ex* dev = NULL;
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

    cmd_rx_overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!cmd_rx_overlapped.hEvent) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    cmd_tx_overlapped.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!cmd_tx_overlapped.hEvent) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
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
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    if (!WinUsb_Initialize(dev->dev_handle, &dev->usb_handle)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
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
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    if (transferred != sizeof(deviceDesc)) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    //
    // Print a few parts of the device descriptor
    //
    /*wprintf(L"Device found: VID_%04X&PID_%04X; bcdUsb %04X\n",
        deviceDesc.idVendor,
        deviceDesc.idProduct,
        deviceDesc.bcdUSB);*/

    USB_INTERFACE_DESCRIPTOR ifaceDescriptor;
    if (!WinUsb_QueryInterfaceSettings(
        dev->usb_handle,
        0,
        &ifaceDescriptor)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    if (ifaceDescriptor.bNumEndpoints < 4) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    // get cmd pipe
    WINUSB_PIPE_INFORMATION_EX pipeInfo;
    if (!WinUsb_QueryPipeEx(dev->usb_handle, 
        0,
        0,
        &pipeInfo)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    if (pipeInfo.PipeType != UsbdPipeTypeBulk) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    dev->exposed.cmd_buffer_size = pipeInfo.MaximumPacketSize;
    dev->exposed.cmd_pipe = ~0x80 & pipeInfo.PipeId;

    // get other pipes
    dev->exposed.msg_pipe_count = (ifaceDescriptor.bNumEndpoints - 2) / 2;
    dev->exposed.msg_pipe_ptr = malloc(sizeof(*dev->exposed.msg_pipe_ptr) * dev->exposed.msg_pipe_count);
    for (unsigned i = 2, j = 0; i < ifaceDescriptor.bNumEndpoints; i += 2, ++j) {
        WINUSB_PIPE_INFORMATION_EX pipeInfo;
        if (!WinUsb_QueryPipeEx(
            dev->usb_handle,
            0,
            i,
            &pipeInfo)) {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            error = HrToError(hr);
            goto Error;
        }

        if (pipeInfo.PipeType != UsbdPipeTypeBulk) {
            error = SC_DLL_ERROR_DEV_UNSUPPORTED;
            goto Error;
        }

        dev->exposed.msg_pipe_ptr[j] = ~0x80 & pipeInfo.PipeId;

        // make bulk in pipe raw i/o
        // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/winusb-functions-for-pipe-policy-modification
        BOOL value = TRUE;
        if (!WinUsb_SetPipePolicy(
            dev->usb_handle,
            dev->exposed.msg_pipe_ptr[j] | 0x80,
            RAW_IO,
            sizeof(value),
            &value)) {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            error = HrToError(hr);
            goto Error;
        }
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

    BOOL value = TRUE;
    if (!WinUsb_SetPipePolicy(
        dev->usb_handle,
        dev->exposed.cmd_pipe | 0x80,
        RAW_IO,
        sizeof(value),
        &value)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    if (!WinUsb_ReadPipe(
        dev->usb_handle,
        dev->exposed.cmd_pipe | 0x80,
        cmd_rx_buffer,
        pipeInfo.MaximumPacketSize,
        NULL,
        &cmd_rx_overlapped) && 
        ERROR_IO_PENDING != GetLastError()) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    struct sc_msg_header* hello = (struct sc_msg_header* )cmd_tx_buffer;
    hello->id = SC_MSG_HELLO_DEVICE;
    hello->len = SC_HEADER_LEN;
    

    if (!WinUsb_WritePipe(
        dev->usb_handle,
        dev->exposed.cmd_pipe,
        cmd_tx_buffer,
        SC_HEADER_LEN,
        NULL,
        &cmd_tx_overlapped)) {
        if (ERROR_IO_PENDING != GetLastError()) {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
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
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    } break;
    }

    if (!WinUsb_GetOverlappedResult(
        dev->usb_handle,
        &cmd_tx_overlapped,
        &transferred,
        FALSE)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    if (transferred != SC_HEADER_LEN) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
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
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    } break;
    }
    
    if (!WinUsb_GetOverlappedResult(
        dev->usb_handle,
        &cmd_rx_overlapped,
        &transferred,
        FALSE)) {
        HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
        error = HrToError(hr);
        goto Error;
    }

    if (sizeof(struct sc_msg_hello) > transferred) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    struct sc_msg_hello *host_hello = (struct sc_msg_hello*)cmd_rx_buffer;
    if (host_hello->id != SC_MSG_HELLO_HOST ||
        host_hello->len != sizeof(*host_hello)) {
        error = SC_DLL_ERROR_DEV_UNSUPPORTED;
        goto Error;
    }

    if (host_hello->proto_version > SC_VERSION) {
        error = SC_DLL_ERROR_VERSION_UNSUPPORTED;
        goto Error;
    }

    
    dev->exposed.msg_buffer_size = Swap16(host_hello->msg_buffer_size); // network byte order
    if (dev->exposed.msg_buffer_size < 64) {
        error = SC_DLL_ERROR_VERSION_UNSUPPORTED;
        goto Error;
    }

    if (SC_BYTE_ORDER_LE == host_hello->byte_order) {
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
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            error = HrToError(hr);
        }
    }
    else if (0 == millis) {
        if (!WinUsb_GetOverlappedResult(dev->usb_handle, ov, transferred, FALSE)) {
            if (ERROR_IO_INCOMPLETE == GetLastError()) {
                error = SC_DLL_ERROR_IO_PENDING;
            }
            else {
                HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
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
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
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
    default:
        return "sc_strerror not implemented";
    }
}