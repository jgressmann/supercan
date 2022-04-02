/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2022 Jean Gressmann <jean@0x42.de>
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

// add headers that you want to pre-compile here
#include "framework.h"



#define LOG2(src, level, ...) \
	do { \
		char buf[256]; \
		int chars = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "SC " src " LVL=%d: ", level); \
		_snprintf_s(buf + chars, sizeof(buf) - chars, _TRUNCATE, __VA_ARGS__); \
		OutputDebugStringA(buf); \
	} while (0)



#define LOG_DLL(level, ...) LOG2("DLL", level, __VA_ARGS__)
#define LOG_SRV(level, ...) LOG2("SRV", level, __VA_ARGS__)


