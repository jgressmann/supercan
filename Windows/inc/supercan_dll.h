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

#ifdef __cplusplus
#define SC_EXTERN_C extern "C"
#else
#define SC_EXTERN_C extern
#endif

#ifdef _MSC_VER
#   ifdef SC_DLL_EXPORTS
#       define SC_DLL_API SC_EXTERN_C __declspec(dllexport)
#   else
#       define SC_DLL_API SC_EXTERN_C __declspec(dllimport)
#   endif
#   else
#       error Define SC_DLL_EXPORTS
#endif




#define SC_DLL_ERROR_UNKNOWN                -1
#define SC_DLL_ERROR_NONE                   0
#define SC_DLL_ERROR_INVALID_PARAM          1
#define SC_DLL_ERROR_OUT_OF_MEM             2
#define SC_DLL_ERROR_DEV_COUNT_CHANGED      4
#define SC_DLL_ERROR_DEV_UNSUPPORTED        5
#define SC_DLL_ERROR_VERSION_UNSUPPORTED    6
#define SC_DLL_ERROR_IO_PENDING             7
#define SC_DLL_ERROR_DEVICE_FAILURE         8

typedef uint16_t(*sc_dev_to_host16)(uint16_t value);
typedef uint32_t(*sc_dev_to_host32)(uint32_t value);

typedef struct sc_dev {
    sc_dev_to_host16 dev_to_host16;
    sc_dev_to_host32 dev_to_host32;
    uint8_t* msg_pipe_ptr;    
    uint16_t cmd_buffer_size;
    uint16_t msg_buffer_size;
    uint8_t cmd_pipe;
    uint8_t msg_pipe_count;
} sc_dev_t;

SC_DLL_API void sc_init(void);
SC_DLL_API void sc_uninit(void);
SC_DLL_API int sc_dev_scan(void);
SC_DLL_API int sc_dev_count(uint32_t* count);
SC_DLL_API int sc_dev_open(uint32_t index, sc_dev_t** dev);
SC_DLL_API void sc_dev_close(sc_dev_t* dev);
SC_DLL_API char const* sc_strerror(int error);
SC_DLL_API int sc_dev_read(sc_dev_t *dev, uint8_t pipe, uint8_t *buffer, ULONG bytes, OVERLAPPED* ov);
SC_DLL_API int sc_dev_write(sc_dev_t *dev, uint8_t pipe, uint8_t const * buffer, ULONG bytes, OVERLAPPED* ov);
SC_DLL_API int sc_dev_result(sc_dev_t *dev, DWORD* bytes, OVERLAPPED* ov, int timeout_ms);

