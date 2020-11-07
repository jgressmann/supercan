/* The MIT License (MIT)
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
 */


#ifndef CHUNKY_NO_STD_INCLUDES
#   include <stddef.h>
#   include <stdlib.h>
#   include <string.h>
#   include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define CHUNKY_JOIN2(x, y) x##y
#define CHUNKY_JOIN(x, y) CHUNKY_JOIN2(x, y)

#ifndef CHUNKY_CHUNK_SIZE_TYPE
#   define CHUNKY_CHUNK_SIZE_TYPE size_t
#endif

#ifndef CHUNKY_BUFFER_SIZE_TYPE
#   define CHUNKY_BUFFER_SIZE_TYPE size_t
#endif

#ifndef CHUNKY_HEADER_PADDING
#   define CHUNKY_HEADER_PADDING 0
#endif

#ifndef CHUNKY_PREFIX
#   define CHUNKY_PREFIX chunky
#endif

#ifndef CHUNKY_ASSERT
#   define CHUNKY_ASSERT assert
#endif

#ifndef CHUNKY_LIKELY
#   define CHUNKY_LIKELY(x) x
#endif

#ifndef CHUNKY_UNLIKELY
#   define CHUNKY_UNLIKELY(x) x
#endif

enum {
    CHUNKYE_NONE = 0,
    CHUNKYE_SEQ = 1,    //< sequence violation
};

#ifdef CHUNKY_BYTESWAP
#define chunky_byteswap CHUNKY_JOIN(CHUNKY_PREFIX, _byteswap)
typedef CHUNKY_CHUNK_SIZE_TYPE (*chunky_byteswap)(void* ctx, CHUNKY_CHUNK_SIZE_TYPE value);

#define chunky_no_swap CHUNKY_JOIN(CHUNKY_PREFIX, _no_swap)
static inline CHUNKY_CHUNK_SIZE_TYPE chunky_no_swap(void* ctx, CHUNKY_CHUNK_SIZE_TYPE value)
{
    (void)ctx;
    return value;
}

#define CHUNKY_BYTESWAP_CALL(x) t->byteswap(t->ctx, x)

#else
#define CHUNKY_BYTESWAP_CALL(x) x
#endif

#define chunky_chunk_hdr CHUNKY_JOIN(CHUNKY_PREFIX, _chunk_hdr)
typedef struct chunky_chunk_hdr {
    CHUNKY_CHUNK_SIZE_TYPE seq_no;
    CHUNKY_CHUNK_SIZE_TYPE len;
#if CHUNKY_HEADER_PADDING > 0
    uint8_t padding[CHUNKY_HEADER_PADDING];
#endif
} chunky_chunk_hdr;

#define chunky_writer CHUNKY_JOIN(CHUNKY_PREFIX, _writer)
typedef struct CHUNKY_WRITER {
#ifdef CHUNKY_BYTESWAP
    void* ctx;
    chunky_byteswap byteswap;
#endif
    uint8_t *hdr;
    uint8_t *buf_ptr;
    CHUNKY_BUFFER_SIZE_TYPE buf_capacity;
    CHUNKY_BUFFER_SIZE_TYPE buf_available;
    CHUNKY_CHUNK_SIZE_TYPE seq_no;
    CHUNKY_CHUNK_SIZE_TYPE len;
    CHUNKY_CHUNK_SIZE_TYPE chunk_size;
} chunky_writer;

/******************************************************************************/

#define chunky_reader CHUNKY_JOIN(CHUNKY_PREFIX, _reader)
typedef struct CHUNKY_READER {
#ifdef CHUNKY_BYTESWAP
    void* ctx;
    chunky_byteswap byteswap;
#endif
    CHUNKY_CHUNK_SIZE_TYPE seq_no;
} chunky_reader;


#define chunky_reader_init CHUNKY_JOIN(CHUNKY_PREFIX, _reader_init)
static inline void chunky_reader_init(
        chunky_reader *t
#ifdef CHUNKY_BYTESWAP
        , void* ctx, chunky_byteswap byteswap
#endif
        )
{
    CHUNKY_ASSERT(t);

    memset(t, 0, sizeof(*t));
#ifdef CHUNKY_BYTESWAP
    t->ctx = ctx;
    t->byteswap = byteswap ? byteswap : &chunky_no_swap;
#endif
}

#define chunky_reader_set_seq_no CHUNKY_JOIN(CHUNKY_PREFIX, _reader_set_seq_no)
static inline void chunky_reader_set_seq_no(chunky_reader *t, CHUNKY_CHUNK_SIZE_TYPE value)
{
    CHUNKY_ASSERT(t);

    t->seq_no = value;
}


#define chunky_reader_chunk_process CHUNKY_JOIN(CHUNKY_PREFIX, _reader_chunk_process)
static inline int chunky_reader_chunk_process(
        chunky_reader *t,
        uint8_t * in_ptr,
        uint8_t ** out_ptr,
        CHUNKY_CHUNK_SIZE_TYPE * out_size)
{
    struct chunky_chunk_hdr const *hdr = NULL;
    CHUNKY_CHUNK_SIZE_TYPE target_seq_no = 0;
    CHUNKY_CHUNK_SIZE_TYPE buffer_seq_no = 0;

    CHUNKY_ASSERT(t);
    CHUNKY_ASSERT(in_ptr);
    CHUNKY_ASSERT(out_ptr);
    CHUNKY_ASSERT(out_size);

    hdr = (struct chunky_chunk_hdr const *)in_ptr;
    target_seq_no = t->seq_no + 1; // must be in size type
    buffer_seq_no = CHUNKY_BYTESWAP_CALL(hdr->seq_no);

    if (CHUNKY_UNLIKELY(target_seq_no != buffer_seq_no)) {
        return CHUNKYE_SEQ;
    }

    t->seq_no = target_seq_no;

    *out_ptr = in_ptr + sizeof(chunky_chunk_hdr);
    *out_size = CHUNKY_BYTESWAP_CALL(hdr->len);

    return CHUNKYE_NONE;
}

/******************************************************************************/

#define chunky_writer_init CHUNKY_JOIN(CHUNKY_PREFIX, _writer_init)
static inline void chunky_writer_init(
        chunky_writer *t,
        CHUNKY_CHUNK_SIZE_TYPE chunk_size
#ifdef CHUNKY_BYTESWAP
        , void* ctx, chunky_byteswap byteswap
#endif
        )
{
    CHUNKY_ASSERT(t);
    CHUNKY_ASSERT(chunk_size >= sizeof(struct chunky_chunk_hdr));

    memset(t, 0, sizeof(*t));
    t->seq_no = 1;
    t->chunk_size = chunk_size;
#ifdef CHUNKY_BYTESWAP
    t->ctx = ctx;
    t->byteswap = byteswap ? byteswap : &chunky_no_swap;
#endif
}

#define chunky_writer_set CHUNKY_JOIN(CHUNKY_PREFIX, _writer_set)
static inline void chunky_writer_set(
        chunky_writer *t,
        void* ptr,
        CHUNKY_BUFFER_SIZE_TYPE buffer_size)
{
    CHUNKY_ASSERT(t);
    CHUNKY_ASSERT(ptr);
    CHUNKY_ASSERT(buffer_size);
    CHUNKY_ASSERT(buffer_size >= t->chunk_size);
    CHUNKY_ASSERT((buffer_size % t->chunk_size) == 0);


    t->buf_ptr = (uint8_t*)ptr;
    t->buf_capacity = buffer_size;
    t->buf_available = buffer_size / t->chunk_size;
    t->buf_available *=  t->chunk_size - sizeof(struct chunky_chunk_hdr);
    t->hdr = t->buf_ptr;
    t->len = 0;
}

#define chunky_writer_available CHUNKY_JOIN(CHUNKY_PREFIX, _writer_available)
static inline CHUNKY_BUFFER_SIZE_TYPE chunky_writer_available(chunky_writer const *t)
{
    CHUNKY_ASSERT(t);

    return t->buf_available;
}

#define chunky_writer_any CHUNKY_JOIN(CHUNKY_PREFIX, _writer_any)
static inline bool chunky_writer_any(chunky_writer const *t)
{
    CHUNKY_ASSERT(t);
    CHUNKY_ASSERT(t->buf_ptr);

    return t->hdr > t->buf_ptr || t->len;
}


#define chunky_writer_write CHUNKY_JOIN(CHUNKY_PREFIX, _writer_write)
static
inline
CHUNKY_BUFFER_SIZE_TYPE
chunky_writer_write(
        chunky_writer *t,
        void const * _ptr,
        CHUNKY_CHUNK_SIZE_TYPE bytes)
{
    uint8_t const *ptr = (uint8_t const *)_ptr;
    CHUNKY_CHUNK_SIZE_TYPE left = 0;

    CHUNKY_ASSERT(t);
    CHUNKY_ASSERT(t->buf_ptr);
    CHUNKY_ASSERT(ptr);
    CHUNKY_ASSERT(bytes);

    if (t->buf_available < bytes) {
        bytes = t->buf_available;
    }

    t->buf_available -= bytes;
    left = bytes;

    while (left) {

        CHUNKY_CHUNK_SIZE_TYPE chunk_available = t->chunk_size - sizeof(struct chunky_chunk_hdr) - t->len;
        CHUNKY_CHUNK_SIZE_TYPE bytes_to_copy = left;
        if (CHUNKY_LIKELY(left > chunk_available)) {
            bytes_to_copy = chunk_available;
        }

        memcpy(t->hdr + sizeof(struct chunky_chunk_hdr) + t->len, ptr, bytes_to_copy);
        left -= bytes_to_copy;
        ptr += bytes_to_copy;
        t->len += bytes_to_copy;

        if (t->chunk_size == sizeof(struct chunky_chunk_hdr) + t->len) {
            struct chunky_chunk_hdr *hdr = (struct chunky_chunk_hdr *)t->hdr;
            hdr->len = CHUNKY_BYTESWAP_CALL(t->len);
            hdr->seq_no = CHUNKY_BYTESWAP_CALL(t->seq_no);
            t->hdr += t->chunk_size;
            t->len = 0;
            ++t->seq_no;
        }
    }

    return bytes;
}

#define chunky_writer_chunk_reserve CHUNKY_JOIN(CHUNKY_PREFIX, _writer_chunk_reserve)
static
inline
void*
chunky_writer_chunk_reserve(
        chunky_writer *t,
        CHUNKY_CHUNK_SIZE_TYPE bytes)
{
    uint8_t *result = NULL;
    CHUNKY_BUFFER_SIZE_TYPE offset = 0;

    CHUNKY_ASSERT(t);
    CHUNKY_ASSERT(t->buf_ptr);
    CHUNKY_ASSERT(bytes);


    offset = sizeof(struct chunky_chunk_hdr) + t->len;

    if (offset + bytes <= t->chunk_size) {
        t->buf_available -= bytes;
        result = t->hdr + offset;
        t->len += bytes;
        if (offset + bytes == t->chunk_size) {
            struct chunky_chunk_hdr *hdr = (struct chunky_chunk_hdr *)t->hdr;

            hdr->len = CHUNKY_BYTESWAP_CALL(t->len);
            hdr->seq_no = CHUNKY_BYTESWAP_CALL(t->seq_no);
            t->hdr += t->chunk_size;
            t->len = 0;
            ++t->seq_no;
        }
    }

    return result;
}

#define chunky_writer_finalize CHUNKY_JOIN(CHUNKY_PREFIX, _writer_finalize)
static
inline
CHUNKY_BUFFER_SIZE_TYPE
chunky_writer_finalize(chunky_writer *t)
{
    CHUNKY_ASSERT(t);
    CHUNKY_ASSERT(t->buf_ptr);

    if (t->len) {
        struct chunky_chunk_hdr *hdr = (struct chunky_chunk_hdr *)t->hdr;

        hdr->len = CHUNKY_BYTESWAP_CALL(t->len);
        hdr->seq_no = CHUNKY_BYTESWAP_CALL(t->seq_no);
        t->len = 0;
        ++t->seq_no;
        t->hdr += t->chunk_size;;
    }

    return t->hdr - t->buf_ptr;
}

#define chunky_writer_chunks_required CHUNKY_JOIN(CHUNKY_PREFIX, _writer_chunks_required)
static
inline
CHUNKY_BUFFER_SIZE_TYPE
chunky_writer_chunks_required(
        chunky_writer *t,
        CHUNKY_BUFFER_SIZE_TYPE bytes)
{
    CHUNKY_BUFFER_SIZE_TYPE bytes_per_chunk = 0;
    CHUNKY_BUFFER_SIZE_TYPE r = 0;

    CHUNKY_ASSERT(t);

    bytes_per_chunk = t->chunk_size - sizeof(struct chunky_chunk_hdr);
    r = bytes / bytes_per_chunk;
    r += r * bytes_per_chunk != bytes;

    return r;
}

#undef CHUNKY_BYTESWAP_CALL

#undef chunky_byteswap
#undef chunky_no_swap
#undef chunky_chunk_hdr
#undef chunky_reader
#undef chunky_reader_init
#undef chunky_reader_set_seq_no
#undef chunky_reader_chunk_process
#undef chunky_writer
#undef chunky_writer_init
#undef chunky_writer_set
#undef chunky_writer_available
#undef chunky_writer_any
#undef chunky_writer_write
#undef chunky_writer_chunk_reserve
#undef chunky_writer_finalize
#undef chunky_writer_chunks_required



#ifdef __cplusplus
}
#endif

