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


#ifndef TOE_RB_SIZE_TYPE
#   define TOE_RB_SIZE_TYPE_DEFINED
#   include <stddef.h>
#   define TOE_RB_SIZE_TYPE size_t
#endif


#ifndef TOE_RB_ASSERT
#   define TOE_RB_ASSERT_DEFINED
#   include <assert.h>
#   define TOE_RB_ASSERT assert
#endif

#ifndef TOE_RB_MEMCPY
#   define TOE_RB_MEMCPY_DEFINED
#   include <string.h>
#   define TOE_RB_MEMCPY memcpy
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TOE_RB_VALUE_TYPE
#   error "Define TOE_RB_VALUE_TYPE to the element type in the buffer"
#endif

#ifndef TOE_RB_PREFIX
#   define TOE_RB_PREFIX_DEFINED
#   define TOE_RB_PREFIX toe_rb
#endif



#define TOE_RB_JOIN2(x, y) x##y
#define TOE_RB_JOIN(x, y) TOE_RB_JOIN2(x, y)


#define TOE_RB_BUF_TYPE TOE_RB_JOIN(TOE_RB_PREFIX, _buf)
typedef struct TOE_RB_JOIN(_, TOE_RB_JOIN(TOE_RB_PREFIX, _buf)) {
    TOE_RB_VALUE_TYPE* ptr;
    TOE_RB_SIZE_TYPE zero, size, capacity;
} TOE_RB_BUF_TYPE;


#define TOE_RB_STATIC_INITIALIZER { NULL, 0, 0, 0 }
#define toe_rb_init(ptrToRingBuffer, ptrToData, Capacity) \
    do { \
        (ptrToRingBuffer)->zero = 0; \
        (ptrToRingBuffer)->size = 0; \
        (ptrToRingBuffer)->capacity = Capacity; \
        (ptrToRingBuffer)->ptr = ptrToData; \
    } while (0)

#define toe_rb_size(ptrToRingBuffer) ((ptrToRingBuffer)->size)
#define toe_rb_capacity(ptrToRingBuffer) ((ptrToRingBuffer)->capacity)
#define toe_rb_used(ptrToRingBuffer) ((ptrToRingBuffer)->size)
#define toe_rb_clear(ptrToRingBuffer) (ptrToRingBuffer)->size = 0
#define toe_rb_left(ptrToRingBuffer) ((ptrToRingBuffer)->capacity - (ptrToRingBuffer)->size)

#define toe_rb_map TOE_RB_JOIN(TOE_RB_PREFIX, _map)
static
inline
TOE_RB_SIZE_TYPE
toe_rb_map(TOE_RB_BUF_TYPE const *rb, TOE_RB_SIZE_TYPE offset) {
    TOE_RB_ASSERT(rb);
    TOE_RB_ASSERT(offset < rb->capacity);
    TOE_RB_SIZE_TYPE sum = offset + rb->zero;
    return sum < rb->capacity ? sum : sum - rb->capacity;
}

#define toe_rb_at_ptr TOE_RB_JOIN(TOE_RB_PREFIX, _at_ptr)
static
inline
TOE_RB_VALUE_TYPE*
toe_rb_at_ptr(TOE_RB_BUF_TYPE *rb, TOE_RB_SIZE_TYPE offset) {
    TOE_RB_ASSERT(rb);
    return &rb->ptr[toe_rb_map(rb, offset)];
}

#define toe_rb_at_cptr TOE_RB_JOIN(TOE_RB_PREFIX, _at_cptr)
static
inline
TOE_RB_VALUE_TYPE const *
toe_rb_at_cptr(TOE_RB_BUF_TYPE const *rb, TOE_RB_SIZE_TYPE offset) {
    TOE_RB_ASSERT(rb);
    return &rb->ptr[toe_rb_map(rb, offset)];
}

#define toe_rb_at_value TOE_RB_JOIN(TOE_RB_PREFIX, _at_value)
static
inline
TOE_RB_VALUE_TYPE
toe_rb_at_value(TOE_RB_BUF_TYPE const *rb, TOE_RB_SIZE_TYPE offset) {
    return rb->ptr[toe_rb_map(rb, offset)];
}

#define toe_rb_push_back_ptr TOE_RB_JOIN(TOE_RB_PREFIX, _push_back_ptr)
static
inline
void
toe_rb_push_back_ptr(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE const *value) {
    TOE_RB_ASSERT(value);
    rb->ptr[toe_rb_map(rb, rb->size++)] = *value;
}

#define toe_rb_push_back_value TOE_RB_JOIN(TOE_RB_PREFIX, _push_back_value)
static
inline
void
toe_rb_push_back_value(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE value) {
    toe_rb_push_back_ptr(rb, &value);
}


#define toe_rb_try_push_back_ptr TOE_RB_JOIN(TOE_RB_PREFIX, _try_push_back_ptr)
static
inline
int
toe_rb_try_push_back_ptr(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE const *value) {
    if (toe_rb_left(rb)) {
        toe_rb_push_back_ptr(rb, value);
        return 1;
    }

    return 0;
}

#define toe_rb_try_push_back_value TOE_RB_JOIN(TOE_RB_PREFIX, _try_push_back_value)
static
inline
int
toe_rb_try_push_back_value(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE value) {
    return toe_rb_try_push_back_ptr(rb, &value);
}

#define toe_rb_push_front_ptr TOE_RB_JOIN(TOE_RB_PREFIX, _push_front_ptr)
static
inline
void
toe_rb_push_front_ptr(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE const *value) {
    TOE_RB_ASSERT(rb);
    TOE_RB_ASSERT(value);
    rb->zero = rb->zero > 0 ? rb->zero - 1 : rb->capacity - 1;
    rb->ptr[rb->zero] = *value;
    ++rb->size;
}

#define toe_rb_push_front_value TOE_RB_JOIN(TOE_RB_PREFIX, _push_front_value)
static
inline
void
toe_rb_push_front_value(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE value) {
    toe_rb_push_front_ptr(rb, &value);
}

#define toe_rb_try_push_front_ptr TOE_RB_JOIN(TOE_RB_PREFIX, _try_push_front_ptr)
static
inline
int
toe_rb_try_push_front_ptr(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE const *value) {
    if (toe_rb_left(rb)) {
        toe_rb_push_front_ptr(rb, value);
        return 1;
    }

    return 0;
}

#define toe_rb_try_push_front_value TOE_RB_JOIN(TOE_RB_PREFIX, _try_push_front_value)
static
inline
int
toe_rb_try_push_front_value(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE value) {
    return toe_rb_try_push_front_ptr(rb, &value);
}

#define toe_rb_drop_back TOE_RB_JOIN(TOE_RB_PREFIX, _drop_back)
static
inline
TOE_RB_SIZE_TYPE
toe_rb_drop_back(TOE_RB_BUF_TYPE *rb) {
    TOE_RB_ASSERT(rb);
    TOE_RB_ASSERT(rb->size > 0);
    return --rb->size;
}

#define toe_rb_drop_front TOE_RB_JOIN(TOE_RB_PREFIX, _drop_front)
static
inline
TOE_RB_SIZE_TYPE
toe_rb_drop_front(TOE_RB_BUF_TYPE *rb) {
    TOE_RB_ASSERT(rb);
    TOE_RB_ASSERT(rb->size > 0);
    TOE_RB_SIZE_TYPE index = rb->zero++;
    if (rb->zero == rb->capacity) {
        rb->zero = 0;
    }
    --rb->size;
    return index;
}


#define toe_rb_pop_back TOE_RB_JOIN(TOE_RB_PREFIX, _pop_back)
static
inline
TOE_RB_VALUE_TYPE
toe_rb_pop_back(TOE_RB_BUF_TYPE *rb) {
    return rb->ptr[toe_rb_map(rb, toe_rb_drop_back(rb))];
}

#define toe_rb_try_pop_back TOE_RB_JOIN(TOE_RB_PREFIX, _try_pop_back)
static
inline
int
toe_rb_try_pop_back(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE *value) {
    TOE_RB_ASSERT(rb);
    if (toe_rb_used(rb)) {
        TOE_RB_VALUE_TYPE dummy;
        if (!value) {
            value = &dummy;
        }
        *value = toe_rb_pop_back(rb);
        return 1;
    }

    return 0;
}

#define toe_rb_pop_front TOE_RB_JOIN(TOE_RB_PREFIX, _pop_front)
static
inline
TOE_RB_VALUE_TYPE
toe_rb_pop_front(TOE_RB_BUF_TYPE *rb) {
    return rb->ptr[toe_rb_drop_front(rb)];
}

#define toe_rb_try_pop_front TOE_RB_JOIN(TOE_RB_PREFIX, _try_pop_front)
static
inline
int
toe_rb_try_pop_front(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE *value) {
    TOE_RB_ASSERT(rb);
    if (toe_rb_used(rb)) {
        TOE_RB_VALUE_TYPE dummy;
        if (!value) {
            value = &dummy;
        }
        *value = toe_rb_pop_front(rb);
        return 1;
    }

    return 0;
}


#define toe_rb_push_back_n TOE_RB_JOIN(TOE_RB_PREFIX, _push_back_n)
static
inline
TOE_RB_SIZE_TYPE
toe_rb_push_back_n(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE const *value, TOE_RB_SIZE_TYPE count) {
    TOE_RB_ASSERT(rb);
    TOE_RB_ASSERT(value);
    TOE_RB_SIZE_TYPE left = toe_rb_left(rb);
    if (count > left) {
        count = left;
    }

    TOE_RB_SIZE_TYPE end = rb->zero + rb->size;
    if (end >= rb->capacity) {
        end -= rb->capacity;
    }

    if (end + count <= rb->capacity) {
        TOE_RB_MEMCPY(rb->ptr + end, value, count * sizeof(TOE_RB_VALUE_TYPE));
    } else {
        TOE_RB_SIZE_TYPE part1 = rb->capacity - end;
        TOE_RB_SIZE_TYPE part2 = count - part1;
        TOE_RB_MEMCPY(rb->ptr + end, value, part1 * sizeof(TOE_RB_VALUE_TYPE));
        TOE_RB_MEMCPY(rb->ptr, value + part1, part2 * sizeof(TOE_RB_VALUE_TYPE));
    }

    rb->size += count;

    return count;
}

#define toe_rb_pop_front_n TOE_RB_JOIN(TOE_RB_PREFIX, _pop_front_n)
static
inline
TOE_RB_SIZE_TYPE
toe_rb_pop_front_n(TOE_RB_BUF_TYPE *rb, TOE_RB_VALUE_TYPE *value, TOE_RB_SIZE_TYPE count) {
    TOE_RB_ASSERT(rb);
    TOE_RB_ASSERT(value);
    if (count > rb->size) {
        count = rb->size;
    }

    if (rb->zero + count == rb->capacity) {
        TOE_RB_MEMCPY(value, rb->ptr + rb->zero, count * sizeof(TOE_RB_VALUE_TYPE));
        rb->zero = 0;
    } else if (rb->zero + count < rb->capacity) {
        TOE_RB_MEMCPY(value, rb->ptr + rb->zero, count * sizeof(TOE_RB_VALUE_TYPE));
        rb->zero += count;
    } else {
        TOE_RB_SIZE_TYPE part1 = rb->capacity - rb->zero;
        TOE_RB_SIZE_TYPE part2 = count - part1;
        TOE_RB_MEMCPY(value, rb->ptr + rb->zero, part1 * sizeof(TOE_RB_VALUE_TYPE));
        TOE_RB_MEMCPY(value + part1, rb->ptr, part2 * sizeof(TOE_RB_VALUE_TYPE));
        rb->zero = part2;
    }

    rb->size -= count;

    return count;
}

#ifdef TOE_RB_SIZE_TYPE_DEFINED
#   undef TOE_RB_SIZE_TYPE
#endif

#ifdef TOE_RB_ASSERT_DEFINED
#   undef TOE_RB_ASSERT
#endif

#ifdef TOE_RB_MEMCPY_DEFINED
#   undef TOE_RB_MEMCPY
#endif

#ifdef TOE_RB_PREFIX_DEFINED
#   undef TOE_RB_PREFIX
#endif



#undef TOE_RB_BUF_TYPE

#undef toe_rb_map
#undef toe_rb_at_value
#undef toe_rb_at_ptr
#undef toe_rb_at_cptr
#undef toe_rb_push_back_value
#undef toe_rb_push_back_ptr
#undef toe_rb_try_push_back_value
#undef toe_rb_try_push_back_ptr
#undef toe_rb_push_front_value
#undef toe_rb_push_front_ptr
#undef toe_rb_try_push_front_value
#undef toe_rb_try_push_front_ptr
#undef toe_rb_pop_back
#undef toe_rb_try_pop_back
#undef toe_rb_pop_front
#undef toe_rb_try_pop_front
#undef toe_rb_drop_back
#undef toe_rb_drop_front
#undef toe_rb_push_back_n
#undef toe_rb_pop_front_n



#ifdef __cplusplus
}
#endif

