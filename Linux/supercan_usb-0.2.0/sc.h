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

#define SC_PACKED __packed
#include "supercan.h"

#define CHUNKY_NO_STD_INCLUDES
#define CHUNKY_CHUNK_SIZE_TYPE u16
#define CHUNKY_BUFFER_SIZE_TYPE unsigned
#define CHUNKY_BYTESWAP
#define CHUNKY_LIKELY likely
#define CHUNKY_UNLIKELY unlikely
#define CHUNKY_ASSERT(x) 
#define CHUNKY_PREFIX sc_chunk
#include "chunky.h"
#undef CHUNKY_NO_STD_INCLUDES
#undef CHUNKY_CHUNK_SIZE_TYPE
#undef CHUNKY_BUFFER_SIZE_TYPE
#undef CHUNKY_BYTESWAP
#undef CHUNKY_LIKELY
#undef CHUNKY_UNLIKELY
#undef CHUNKY_ASSERT
#undef CHUNKY_PREFIX

