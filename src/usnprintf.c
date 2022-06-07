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

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>

#include <usnprintf.h>

#define FLAG_HEX_MASC   0x1
//#define FLAG_SIGN	   0x2

static const char hex[16] = "0123456789abcdef";
static const char HEX[16] = "0123456789ABCDEF";

#ifndef USNPRINTF_WITH_LONGLONG
    #define USNPRINTF_WITH_LONGLONG 0
#endif

#if USNPRINTF_WITH_LONGLONG
    #define USNPRINTF_UINT_TYPE unsigned long long
    #define USNPRINTF_INT_TYPE long long
#else
    #define USNPRINTF_UINT_TYPE unsigned long
    #define USNPRINTF_INT_TYPE long
#endif

USNPRINTF_SECTION
static
inline
void
uprint_uint_reverse_char(
		char * restrict buffer,
		size_t * restrict offset_ptr,
		char const * restrict alphabet,
        USNPRINTF_UINT_TYPE* restrict value,
		unsigned base)
{
    USNPRINTF_UINT_TYPE a = *value / base;
	unsigned digit = *value - a * base;
	*value = a;
	buffer[*offset_ptr] = alphabet[digit];
	++*offset_ptr;
}

USNPRINTF_SECTION
static
void
uprint_uint_raw(
		char * restrict buffer,
		size_t * restrict offset_ptr,
		size_t end,
		int flags,
		unsigned base,
        USNPRINTF_UINT_TYPE value)
{
	size_t start_offset = *offset_ptr;
	unsigned chars = 0;

	char const *alphabet;
	if (16 == base && (flags & FLAG_HEX_MASC)) {
		alphabet = HEX;
	} else {
		alphabet = hex;
	}

	if (*offset_ptr < end) { // zero case
        uprint_uint_reverse_char(buffer, offset_ptr, alphabet, &value, base);
		++chars;
	}

	while (*offset_ptr < end && value) {
        uprint_uint_reverse_char(buffer, offset_ptr, alphabet, &value, base);
		++chars;
	}

	if (chars > 1) {
		size_t end_offset = *offset_ptr;
		size_t count = (end_offset - start_offset) / 2;
		for (size_t i = start_offset, j = end_offset - 1, k = 0; k < count; ++i, --j, ++k) {
			char c = buffer[i];
			buffer[i] = buffer[j];
			buffer[j] = c;
		}
	}
}

USNPRINTF_SECTION
int usnprintf(
	char * restrict buffer,
	size_t size,
	char const * restrict fmt,
	...)
{
	if (!size) {
		return -1;
	}

	const size_t end = size - 1;
	size_t offset = 0;
	signed char step = -1;
	unsigned char int_size = 0;
	unsigned char print_sign = 0;
	unsigned char print_hex_prefix = 0;
	// char fill = 0;
    int error = 0;


	va_list vl;
	va_start(vl, fmt);

	while (offset < end) {
		char c = *fmt++;
		if ('\0' == c) {
			break;
		}

		if ('%' == c) {
			if (0 == step) {
				step = -1;
				buffer[offset++] = '%';
			} else {
				step = 0;
				print_sign = 0;
				int_size = 0;
				// fill = 0;
				print_hex_prefix = 0;
			}
		} else {
start:
			switch (step) {
			case -1:
				buffer[offset++] = c;
				break;
			case 0: // flags
				switch (c) {
				case '-':
					// left justify
					break;
				case '+':
					print_sign = 1;
					break;
				case ' ':
					// fill = ' ';
					break;
				case '0':
					// fill = '0';
					break;
				case '#':
					print_hex_prefix = 1;
					break;
				default:
					++step;
					goto start;
				}
				break;
			case 1: // width, precision
precision:
				switch (c) {
				case '*':
                     // not supported
                    error = -1;
                    goto out;
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					break;
				default:
					++step;
					goto start;
				}
				break;
			case 2: // precision
				++step;
				if ('*' == c || (c >= '0' && c <= '9')) {
					goto precision;
				}
				goto start;
			case 3: // length char 1

				switch (c) {
				case 'h':
					--int_size;
					break;
				case 'j':
					int_size = 2;
					break;
				case 'l':
					++int_size;
					break;
				case 't':
					int_size = sizeof(ptrdiff_t) / sizeof(int) - 1;
					break;
				case 'z':
					int_size = sizeof(size_t) / sizeof(int) - 1;
					break;
				case 'c':
					step = -1;
					buffer[offset++] = (char)va_arg(vl, int);
					break;
				case 's':
					step = -1;
					char const *s = (char*)va_arg(vl, void*);
					while (offset < end && *s) {
						buffer[offset++] = *s++;
					}
					break;
				case 'd':
				case 'i': {
                    // short/int/long
                    USNPRINTF_INT_TYPE v;

                    step = -1;

					if (2 == int_size) {
                        v = va_arg(vl, USNPRINTF_INT_TYPE);
					} else if (1 == int_size) {
						v = va_arg(vl, long);
					} else {
						v = va_arg(vl, int);
					}

					if (print_sign) {
						if (v < 0) {
							buffer[offset++] = '-';
							v = -v;
						} else {
							buffer[offset++] = '+';
						}
					} else if (v < 0) {
						buffer[offset++] = '-';
						v = -v;
					}

                    uprint_uint_raw(buffer, &offset, end, 0, 10, (USNPRINTF_UINT_TYPE)v);
				} break;
				case 'u':
				case 'x':
				case 'X': {
                    // unsigned short/int/long
                    USNPRINTF_UINT_TYPE v;
                    unsigned base;

					step = -1;

					int flags = 0;
					if ('u' == c) {
						base = 10;
						if (print_sign) {
							buffer[offset++] = '+';
						}
					} else {
						if (print_hex_prefix) {
							if (offset + 2 <= end) {
								buffer[offset++] = '0';
								buffer[offset++] = 'x';
							} else {
                                goto done;
							}
						}
						base = 16;
						if ('X' == c) {
							flags |= FLAG_HEX_MASC;
						}
					}

					if (2 == int_size) {
                        v = va_arg(vl, USNPRINTF_UINT_TYPE);
					} else if (1 == int_size) {
						v = va_arg(vl, unsigned long);
					} else {
						v = va_arg(vl, unsigned int);
					}
                    uprint_uint_raw(buffer, &offset, end, flags, base, v);
				} break;
				case 'p':
					step = -1;
					buffer[offset++] = '0';
					if (offset < end) {
                        USNPRINTF_UINT_TYPE v = (USNPRINTF_UINT_TYPE)(uintptr_t)va_arg(vl, void*);

                        buffer[offset++] = 'x';
                        uprint_uint_raw(buffer, &offset, end, 0, 16, v);
					}
					break;
				default:
                    error = -1;
                    goto out;
				}
				break;
			default:
                error = -1;
                goto out;
			}
		}
	}

done:
    buffer[offset] = '\0';

    error = (int)offset;

out:
    va_end(vl);

    return error;
}
