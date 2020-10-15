#include <CppUnitLite2.h>
#include <limits>
#include <cstring>


#include "usnprintf.h"

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

TEST (usnprintf_can_prefix_with_0x) 
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

} // anon namespace
