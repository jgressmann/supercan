#include <CppUnitLite2.h>
#include <TestException.h>

#define CHUNKY_ASSERT(x) \
    do { \
        if (!(x)) throw TestException(__FILE__, __LINE__, #x); \
    } while (0)


#define CHUNKY_CHUNK_SIZE_TYPE uint8_t
#define CHUNKY_BYTESWAP

#include "chunky.h"

#ifdef chunky_chunk_hdr
#   error chunky_chunk_hdr still defined
#endif

#ifdef chunky_writer
#   error chunky_writer still defined
#endif

#ifdef chunky_reader
#   error chunky_reader still defined
#endif

#ifdef chunky_reader_init
#   error chunky_reader_init still defined
#endif

#ifdef chunky_reader_set_seq_no
#   error chunky_reader_set_seq_no still defined
#endif

#ifdef chunky_reader_chunk_process
#   error chunky_reader_chunk_process still defined
#endif

#ifdef chunky_writer_init
#   error chunky_writer_init still defined
#endif

#ifdef chunky_writer_set
#   error chunky_writer_set still defined
#endif

#ifdef chunky_writer_available
#   error chunky_writer_available still defined
#endif

#ifdef chunky_writer_any
#   error chunky_writer_any still defined
#endif

#ifdef chunky_writer_write
#   error chunky_writer_write still defined
#endif

#ifdef chunky_writer_chunk_reserve
#   error chunky_writer_chunk_reserve still defined
#endif

#ifdef chunky_writer_finalize
#   error chunky_writer_finalize still defined
#endif





#define CHUNKY_CHUNK_SIZE 12


namespace
{

struct Fixture {
    chunky_writer w;
    chunky_reader r;
    uint8_t writer_buffer[CHUNKY_CHUNK_SIZE*2];

    ~Fixture() {

    }

    Fixture() {
        memset(&w, 0xff, sizeof(w));
        memset(&r, 0xff, sizeof(r));
        chunky_writer_init(&w, CHUNKY_CHUNK_SIZE, this, NULL);
        chunky_reader_init(&r, this, NULL);
    }
};



#if 1

TEST (writer_init_fails_for_invalid_params)
{
    chunky_writer w;
    CHECK_ASSERT(chunky_writer_init(NULL, CHUNKY_CHUNK_SIZE, NULL, NULL));
    CHECK_ASSERT(chunky_writer_init(&w, 0, NULL, NULL));
    CHECK_ASSERT(chunky_writer_init(&w, 1, NULL, NULL));
}

TEST_F (Fixture, writer_init_succeeds)
{
    CHECK_EQUAL(0u, chunky_writer_available(&w));
}

TEST_F (Fixture, writer_set_buffer_fails_for_invalid_arguments)
{
    CHECK_ASSERT(chunky_writer_set(NULL, writer_buffer, sizeof(writer_buffer)));
    CHECK_ASSERT(chunky_writer_set(&w, NULL, sizeof(writer_buffer)));
    CHECK_ASSERT(chunky_writer_set(&w, writer_buffer, 0));
}

TEST_F (Fixture, writer_set_buffer_succeeds)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));
    CHECK_EQUAL(sizeof(writer_buffer)-sizeof(uint8_t)*4, chunky_writer_available(&w));
}

TEST_F (Fixture, writer_write_fails_for_invalid_params)
{
    int x = 42;
    CHECK_ASSERT(chunky_writer_write(NULL, &x, sizeof(x)));
    CHECK_ASSERT(chunky_writer_write(&w, NULL, sizeof(x)));
    CHECK_ASSERT(chunky_writer_write(&w, &x, 0));
}

TEST_F (Fixture, writer_write_succeeds)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));
    unsigned available = chunky_writer_available(&w);

    // same chunk
    uint64_t x = 42;
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(available - sizeof(x), chunky_writer_available(&w));

    uint64_t x2;
    memcpy(&x2, writer_buffer + 2, sizeof(x2));
    CHECK_EQUAL(x, x2);

    // overlap
    ++x;
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(available - 2*sizeof(x), chunky_writer_available(&w));
    uint8_t x3[sizeof(x)];
    memcpy(x3, writer_buffer + 10, 2);
    memcpy(x3 + 2, writer_buffer + 14, 6);
    uint64_t x4;
    memcpy(&x4, x3, sizeof(x3));
    CHECK_EQUAL(x, x4);

    // cut
    ++x;
    CHECK_EQUAL(4u, chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(0u, chunky_writer_available(&w));
}


TEST_F (Fixture, writer_reserve_fails_for_invalid_params)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));

    CHECK_ASSERT(chunky_writer_chunk_reserve(NULL, 1));
    CHECK_ASSERT(chunky_writer_chunk_reserve(&w, 0));
}


TEST_F (Fixture, writer_reserve_succeeds)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));

    unsigned available = chunky_writer_available(&w);
    uint8_t* ptr = (uint8_t*)chunky_writer_chunk_reserve(&w, 8);
    CHECK(ptr);
    CHECK_EQUAL(available - 8, chunky_writer_available(&w));;
    ptr[0] = 0xa0;
    ptr[1] = 0xa1;
    ptr[2] = 0xa3;
    ptr[3] = 0xa3;
    ptr[4] = 0xa4;
    ptr[5] = 0xa5;
    ptr[6] = 0xa6;
    ptr[7] = 0xa7;

    for (int i = 0; i < 8; ++i) {
        CHECK_EQUAL(ptr[i], writer_buffer[2+i]);
    }
    // across chunks
    ptr = (uint8_t*)chunky_writer_chunk_reserve(&w, 8);
    CHECK(!ptr);
    CHECK_EQUAL(available - 8, chunky_writer_available(&w));;
}

TEST_F (Fixture, writer_finalize_fails_on_invalid_params)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));

    CHECK_ASSERT(chunky_writer_finalize(NULL));
}

TEST_F (Fixture, writer_finalize_on_empty_buffer)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));


    CHECK_EQUAL(0u, chunky_writer_finalize(&w));
}

TEST_F (Fixture, writer_finalize_in_chunk)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));
    // same chunk
    uint64_t x = 42;
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL((unsigned)CHUNKY_CHUNK_SIZE, chunky_writer_finalize(&w));
    CHECK_EQUAL(1u, writer_buffer[0]);
    CHECK_EQUAL(8u, writer_buffer[1]);
}

TEST_F (Fixture, writer_finalize_at_chunk_end)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));
    // same chunk
    CHECK(chunky_writer_chunk_reserve(&w, 10));
    CHECK_EQUAL(CHUNKY_CHUNK_SIZE, chunky_writer_finalize(&w));
    CHECK_EQUAL(1u, writer_buffer[0]);
    CHECK_EQUAL(10u, writer_buffer[1]);
}

TEST_F (Fixture, writer_finalize_more_than_1_chunk)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));
    // same chunk
    uint64_t x = 42;
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(2*CHUNKY_CHUNK_SIZE, chunky_writer_finalize(&w));
    CHECK_EQUAL(1u, writer_buffer[0]);
    CHECK_EQUAL(10u, writer_buffer[1]);
    CHECK_EQUAL(2u, writer_buffer[12]);
    CHECK_EQUAL(6u, writer_buffer[13]);
}

TEST_F (Fixture, writer_finalize_all_filled)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));
    // same chunk
    uint64_t x = 42;
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK(chunky_writer_chunk_reserve(&w, 4));
    CHECK_EQUAL(2*CHUNKY_CHUNK_SIZE, chunky_writer_finalize(&w));
    CHECK_EQUAL(1, writer_buffer[0]);
    CHECK_EQUAL(10, writer_buffer[1]);
    CHECK_EQUAL(2, writer_buffer[12]);
    CHECK_EQUAL(10, writer_buffer[13]);
}

TEST_F (Fixture, writer_any)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));

    CHECK(!chunky_writer_any(&w));
    uint64_t x = 42;
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK(chunky_writer_any(&w));
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK(chunky_writer_any(&w));
}

TEST_F (Fixture, writer_chunk_size_equal_to_buffer_size)
{
    chunky_writer_set(&w, writer_buffer, 12);
    CHECK_EQUAL(10, chunky_writer_available(&w));

    uint8_t* ptr = (uint8_t*)chunky_writer_chunk_reserve(&w, 8);
    CHECK(ptr);
    uint32_t x = 42;
    CHECK_EQUAL(2, chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(0, chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(CHUNKY_CHUNK_SIZE, chunky_writer_finalize(&w));
    CHECK_EQUAL(1, writer_buffer[0]);
    CHECK_EQUAL(10, writer_buffer[1]);
}


TEST (reader_init_fails_for_invalid_params)
{
    CHECK_ASSERT(chunky_reader_init(NULL, NULL, NULL));
}

TEST_F (Fixture, reader_init_succeeds)
{
    (void)result_;
}

TEST_F (Fixture, reader_process_chunk_fails_for_invalid_params)
{
    uint8_t* out_ptr = NULL;
    uint8_t out_size = 0;
    CHECK_ASSERT(chunky_reader_chunk_process(NULL, writer_buffer, &out_ptr, &out_size));
    CHECK_ASSERT(chunky_reader_chunk_process(&r, NULL, &out_ptr, &out_size));
    CHECK_ASSERT(chunky_reader_chunk_process(&r, writer_buffer, NULL, &out_size));
    CHECK_ASSERT(chunky_reader_chunk_process(&r, writer_buffer, &out_ptr, NULL));
}

TEST_F (Fixture, reader_process_chunk_succeeds)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));

    uint8_t* ptr = (uint8_t*)chunky_writer_chunk_reserve(&w, 5);
    CHECK(ptr);
    memset(ptr, 0x12, 5);
    uint64_t x = 0x0102030405060708;
    CHECK_EQUAL(sizeof(x), chunky_writer_write(&w, &x, sizeof(x)));
    CHECK_EQUAL(sizeof(writer_buffer), chunky_writer_finalize(&w));
    char reader_buffer[sizeof(writer_buffer)];
    ptr = NULL;
    uint8_t size = 0;
    CHECK_EQUAL(CHUNKYE_NONE, chunky_reader_chunk_process(&r, writer_buffer, &ptr, &size));
    CHECK(ptr);
    CHECK_EQUAL(10u, size);
    memcpy(reader_buffer, ptr, size);
    ptr = NULL;
    size = 0;
    CHECK_EQUAL(CHUNKYE_NONE, chunky_reader_chunk_process(&r, writer_buffer + CHUNKY_CHUNK_SIZE, &ptr, &size));
    CHECK(ptr);
    CHECK_EQUAL(3u, size);
    memcpy(reader_buffer + 10, ptr, size);

    for (size_t i = 0; i < 5; ++i) {
        CHECK_EQUAL(0x12, reader_buffer[i]);
    }

    uint64_t x2;
    memcpy(&x2, reader_buffer+ 5, sizeof(x2));
    CHECK_EQUAL(x, x2);
}

TEST_F (Fixture, reader_process_chunk_fails_if_seq_no_mismatches)
{
    chunky_writer_set(&w, writer_buffer, sizeof(writer_buffer));

    uint8_t* ptr = (uint8_t*)chunky_writer_chunk_reserve(&w, 5);
    CHECK(ptr);
    memset(ptr, 0x12, 5);
    CHECK_EQUAL(12, chunky_writer_finalize(&w));
    ptr = NULL;
    uint8_t size = 0;
    CHECK_EQUAL(CHUNKYE_NONE, chunky_reader_chunk_process(&r, writer_buffer, &ptr, &size));
    CHECK(ptr);
    CHECK_EQUAL(5u, size);

    chunky_reader_set_seq_no(&r, 2);
    CHECK_EQUAL(CHUNKYE_SEQ, chunky_reader_chunk_process(&r, writer_buffer, &ptr, &size));

}


#endif
} // anon namespace




