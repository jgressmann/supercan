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

//
//  W A R N I N G
//  -------------
//
// This file is not part of the Qt API.  It exists purely as an
// implementation detail.  This header file may change from version to
// version without notice, or even be removed.
//
// We mean it.
//

#include <QtCore/qlibrary.h>
#include <QtCore/qstring.h>
#include <QtCore/qdebug.h>

#include <qt_windows.h>

#include "supercan_dll.h"


#define GENERATE_SYMBOL_VARIABLE(returnType, symbolName, ...) \
    typedef returnType (*fp_##symbolName)(__VA_ARGS__); \
    static fp_##symbolName dl_##symbolName;

#define RESOLVE_SYMBOL(symbolName) \
    dl_##symbolName = reinterpret_cast<fp_##symbolName>(supercanLibrary->resolve(#symbolName)); \
    if (!dl_##symbolName) \
        return false;

GENERATE_SYMBOL_VARIABLE(void, sc_init)
GENERATE_SYMBOL_VARIABLE(void, sc_uninit)
GENERATE_SYMBOL_VARIABLE(char const*, sc_strerror, int)
GENERATE_SYMBOL_VARIABLE(void, sc_dev_scan)
GENERATE_SYMBOL_VARIABLE(int, sc_dev_count, uint32_t* count)
GENERATE_SYMBOL_VARIABLE(int, sc_dev_open, uint32_t, sc_dev_t**)
GENERATE_SYMBOL_VARIABLE(int, sc_dev_close, sc_dev_t*)
GENERATE_SYMBOL_VARIABLE(int, sc_dev_read, sc_dev_t *, uint8_t, uint8_t*, ULONG, OVERLAPPED*)
GENERATE_SYMBOL_VARIABLE(int, sc_dev_write, sc_dev_t *, uint8_t, uint8_t const*, ULONG, OVERLAPPED*)
GENERATE_SYMBOL_VARIABLE(int, sc_dev_result, sc_dev_t *, DWORD*, OVERLAPPED*, int)
GENERATE_SYMBOL_VARIABLE(int, sc_dev_cancel, sc_dev_t *, OVERLAPPED*)

inline bool resolveSymbols(QLibrary *supercanLibrary)
{
    if (!supercanLibrary->isLoaded()) {
        if (sizeof(void*) == 4) {
            supercanLibrary->setFileName(QStringLiteral("supercan32"));
        } else {
            supercanLibrary->setFileName(QStringLiteral("supercan64"));
        }
        if (!supercanLibrary->load())
            return false;
    }

    RESOLVE_SYMBOL(sc_init)
    RESOLVE_SYMBOL(sc_uninit)
    RESOLVE_SYMBOL(sc_strerror)
    RESOLVE_SYMBOL(sc_dev_scan)
    RESOLVE_SYMBOL(sc_dev_count)
    RESOLVE_SYMBOL(sc_dev_open)
    RESOLVE_SYMBOL(sc_dev_close)
    RESOLVE_SYMBOL(sc_dev_read)
    RESOLVE_SYMBOL(sc_dev_write)
    RESOLVE_SYMBOL(sc_dev_result)
    RESOLVE_SYMBOL(sc_dev_cancel)

    return true;
}
