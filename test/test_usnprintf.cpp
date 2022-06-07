#include <CppUnitLite2.h>
#include <limits>
#include <cstring>
#include <algorithm>


#include "usnprintf.h"

#ifndef ARRAY_SIZE
    #define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#endif

using namespace std;

namespace
{

TEST (usnprintf_gracefully_handles_small_buffers)
{
    char buf[8];

    int chars = usnprintf(buf, 0, "%s", "Way too long a string!");
    CHECK_EQUAL(-1, chars);

    for (size_t i = 1; i < sizeof(buf); ++i) {
        int chars = usnprintf(buf, i, "%s", "Way too long a string!");
        CHECK_EQUAL((int)i-1, chars);
    }
}

TEST (usnprintf_prints_characters)
{
    char buf[8];

    for (char c = 'a'; c < 'z'; ++c) {
        int chars = usnprintf(buf, sizeof(buf), "%c", c);
        CHECK_EQUAL(1, chars);
        CHECK_EQUAL(c, buf[0]);
    }
}

TEST (usnprintf_prints_strings)
{
    char buf[8];
    const char str[] = "Hello, World!";

    for (size_t i = 0; i < ARRAY_SIZE(str) - 1; ++i) {
        int chars = usnprintf(buf, sizeof(buf), "%s", &str[i]);
        CHECK(chars >= 0);
        CHECK_EQUAL(min<size_t>(chars, min(ARRAY_SIZE(str)-1-i, ARRAY_SIZE(buf)-1)), (size_t)chars);
        CHECK_EQUAL(0, strncmp(buf, &str[i], chars));
    }
}


TEST (usnprintf_prints_signed_chars)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%hhd", std::numeric_limits<signed char>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hhd", std::numeric_limits<signed char>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    // alias
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hhi", std::numeric_limits<signed char>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%hhd", std::numeric_limits<signed char>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hhd", std::numeric_limits<signed char>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    // alias
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hhi", std::numeric_limits<signed char>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_prints_unsigned_chars)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%hhu", std::numeric_limits<unsigned char>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hhu", std::numeric_limits<unsigned char>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%hhu", std::numeric_limits<unsigned char>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hhu", std::numeric_limits<unsigned char>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_prints_shorts)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%hd", std::numeric_limits<signed short>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hd", std::numeric_limits<signed short>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    // alias
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hi", std::numeric_limits<signed short>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%hd", std::numeric_limits<signed short>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hd", std::numeric_limits<signed short>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    // alias
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hi", std::numeric_limits<signed short>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}


TEST (usnprintf_prints_unsigned_shorts)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%hu", std::numeric_limits<unsigned short>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hu", std::numeric_limits<unsigned short>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%hu", std::numeric_limits<unsigned short>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%hu", std::numeric_limits<unsigned short>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_prints_ints)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%d", std::numeric_limits<int>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%d", std::numeric_limits<int>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    // alias
    uchars = usnprintf(ubuf, sizeof(ubuf), "%i", std::numeric_limits<int>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%d", std::numeric_limits<int>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%d", std::numeric_limits<int>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    // alias
    uchars = usnprintf(ubuf, sizeof(ubuf), "%i", std::numeric_limits<int>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}


TEST (usnprintf_prints_unsigned_ints)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%u", std::numeric_limits<unsigned int>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%u", std::numeric_limits<unsigned int>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%u", std::numeric_limits<unsigned int>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%u", std::numeric_limits<unsigned int>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_prints_longs)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%ld", std::numeric_limits<long>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%ld", std::numeric_limits<long>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    uchars = usnprintf(ubuf, sizeof(ubuf), "%li", std::numeric_limits<long>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%ld", std::numeric_limits<long>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%ld", std::numeric_limits<long>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    uchars = usnprintf(ubuf, sizeof(ubuf), "%li", std::numeric_limits<long>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}


TEST (usnprintf_prints_unsigned_longs)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%lu", std::numeric_limits<unsigned long>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%lu", std::numeric_limits<unsigned long>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%lu", std::numeric_limits<unsigned long>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%lu", std::numeric_limits<unsigned long>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}



TEST (usnprintf_prints_long_longs)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%lld", std::numeric_limits<long long>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%lld", std::numeric_limits<long long>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    uchars = usnprintf(ubuf, sizeof(ubuf), "%lli", std::numeric_limits<long long>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%lld", std::numeric_limits<long long>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%lld", std::numeric_limits<long long>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    uchars = usnprintf(ubuf, sizeof(ubuf), "%lli", std::numeric_limits<long long>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}


TEST (usnprintf_prints_unsigned_long_longs)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%llu", std::numeric_limits<unsigned long long>::min());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%llu", std::numeric_limits<unsigned long long>::min());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%llu", std::numeric_limits<unsigned long long>::max());
    uchars = usnprintf(ubuf, sizeof(ubuf), "%llu", std::numeric_limits<unsigned long long>::max());
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_prints_x)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%x", (unsigned)0xfedcba98);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%x", (unsigned)0xfedcba98);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%lx", (unsigned long)0xfedcba98);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%lx", (unsigned long)0xfedcba98);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%llx", (unsigned long long)0xfedcba9876543210);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%llx", (unsigned long long)0xfedcba9876543210);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_prints_X)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%X", (unsigned)0xfedcba98);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%X", (unsigned)0xfedcba98);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%lX", (unsigned long)0xfedcba98);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%lX", (unsigned long)0xfedcba98);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%llX", (unsigned long long)0xfedcba9876543210);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%llX", (unsigned long long)0xfedcba9876543210);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_honors_hash_flag)
{
    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "%#x", (unsigned)0xfedcba98);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%#x", (unsigned)0xfedcba98);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%#lx", (unsigned long)0xfedcba98);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%#lx", (unsigned long)0xfedcba98);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%#llx", (unsigned long long)0xfedcba9876543210);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%#llx", (unsigned long long)0xfedcba9876543210);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

TEST (usnprintf_handles_escaped_percent)
{
    char buf[8];

    int chars = usnprintf(buf, sizeof(buf), "%%");
    CHECK_EQUAL(1, chars);
    CHECK_EQUAL('%', buf[0]);

    chars = usnprintf(buf, sizeof(buf), "%%d", 42);
    CHECK_EQUAL(2, chars);
    CHECK_EQUAL('%', buf[0]);
    CHECK_EQUAL('d', buf[1]);
}

TEST (usnprintf_honors_space_flag)
{
    char buf[8];

    int chars = usnprintf(buf, sizeof(buf), "% d", 10);
    CHECK_EQUAL(3, chars);
    CHECK_EQUAL(' ', buf[0]);
    CHECK_EQUAL('1', buf[1]);
    CHECK_EQUAL('0', buf[2]);

    chars = usnprintf(buf, sizeof(buf), "% d", -2);
    CHECK_EQUAL(2, chars);
    CHECK_EQUAL('-', buf[0]);
    CHECK_EQUAL('2', buf[1]);
}


TEST (usnprintf_honors_plus_flag)
{
    char buf[8];

    int chars = usnprintf(buf, sizeof(buf), "%+d", 10);
    CHECK_EQUAL(3, chars);
    CHECK_EQUAL('+', buf[0]);
    CHECK_EQUAL('1', buf[1]);
    CHECK_EQUAL('0', buf[2]);

    chars = usnprintf(buf, sizeof(buf), "%+d", -2);
    CHECK_EQUAL(2, chars);
    CHECK_EQUAL('-', buf[0]);
    CHECK_EQUAL('2', buf[1]);
}

TEST (usnprintf_prints_with_fill_char)
{
    char buf[10];

    int chars = usnprintf(buf, sizeof(buf), "%03x", 10);
    CHECK_EQUAL(3, chars);
    CHECK_EQUAL('0', buf[0]);
    CHECK_EQUAL('0', buf[1]);
    CHECK_EQUAL('a', buf[2]);

    chars = usnprintf(buf, sizeof(buf), "%08x", 0x1234);
    CHECK_EQUAL(8, chars);
    CHECK_EQUAL('0', buf[0]);
    CHECK_EQUAL('0', buf[1]);
    CHECK_EQUAL('0', buf[2]);
    CHECK_EQUAL('0', buf[3]);
    CHECK_EQUAL('1', buf[4]);
    CHECK_EQUAL('2', buf[5]);
    CHECK_EQUAL('3', buf[6]);
    CHECK_EQUAL('4', buf[7]);

    char ubuf[64];
    char cbuf[64];

    int uchars = 0;
    int cchars = 0;

    cchars = snprintf(cbuf, sizeof(cbuf), "% 3d", -3);
    uchars = usnprintf(ubuf, sizeof(ubuf), "% 3d", -3);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "% 3d", 3);
    uchars = usnprintf(ubuf, sizeof(ubuf), "% 3d", 3);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

//    cchars = snprintf(cbuf, sizeof(cbuf), "%03d", -1);
//    uchars = usnprintf(ubuf, sizeof(ubuf), "%03d", -1);
//    CHECK_EQUAL(cchars, uchars);
//    CHECK_EQUAL(0, strcmp(ubuf, cbuf));

    cchars = snprintf(cbuf, sizeof(cbuf), "%03d", 3);
    uchars = usnprintf(ubuf, sizeof(ubuf), "%03d", 3);
    CHECK_EQUAL(cchars, uchars);
    CHECK_EQUAL(0, strcmp(ubuf, cbuf));
}

} // anon namespace
