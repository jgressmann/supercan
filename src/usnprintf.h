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

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
	#define restrict __restrict__
	#ifdef USNPRINTF_SECTION_NAME
		#define USNPRINTF_SECTION __attribute__((section(USNPRINTF_SECTION_NAME)))
	#else
		#define USNPRINTF_SECTION
	#endif
#endif



/* micro snprintf replacement
 *
 * This implementation supports these specifiers: cdipsuxX.
 * These are the supported _length_ specifers (integers only): (none), h, hh, j, l, ll('), z, t
 * Flags are honored expect - (left justify) which is ignored.
 * Width specifier * is _not_ supported. number specifiers are.
 * Precision specifiers are _not_ supported.
 *
 * (') Define USNPRINTF_WITH_LONGLONG to support (unsigned) long long's.
 **/
USNPRINTF_SECTION int usnprintf(
	char * restrict buffer,
	size_t size,
	char const * restrict fmt,
	...);

#ifdef __cplusplus
} // extern "C"
#endif
