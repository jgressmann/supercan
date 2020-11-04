#include <CppUnitLite2.h>

#include "supercan_misc.h"

namespace
{
struct ts_fixture
{
    struct sc_dev_time_tracker t;

    ts_fixture()
    {
        sc_tt_init(&t);
    }
};


TEST_F (ts_fixture, the_initial_timestamp_is_returned_as_is)
{
    auto r = sc_tt_track(&t, 42);
    CHECK_EQUAL(42, r);
}

TEST_F(ts_fixture, forward_increments_move_time_forward)
{

    auto r0 = sc_tt_track(&t, 1);
    CHECK_EQUAL(1, r0);
    auto r1 = sc_tt_track(&t, 100000);
    CHECK_EQUAL(100000, r1);
    auto r2 = sc_tt_track(&t, UINT32_MAX / 2);
    CHECK_EQUAL(UINT32_MAX / 2, r2);

    auto r3 = sc_tt_track(&t, UINT32_MAX-2);
    CHECK_EQUAL(UINT32_MAX-2, r3);
}

TEST_F(ts_fixture, forward_laps_increments_high)
{

    auto r0 = sc_tt_track(&t, UINT32_MAX);
    CHECK_EQUAL(UINT32_MAX, r0);

    auto r1 = sc_tt_track(&t, UINT32_MAX / 2 - 2);
    CHECK_EQUAL(UINT32_MAX / 2 - 2 + (UINT64_C(1) << 32), r1);

    auto r2 = sc_tt_track(&t, UINT32_MAX / 2 - 1);
    CHECK_EQUAL(UINT32_MAX / 2 - 1 + (UINT64_C(1) << 32), r2);

    auto r3 = sc_tt_track(&t, UINT32_MAX / 2);
    CHECK_EQUAL(UINT32_MAX / 2 + (UINT64_C(1) << 32), r3);
}


TEST_F(ts_fixture, negative_laps_decrement_high)
{
    auto r0 = sc_tt_track(&t, UINT32_MAX);
    CHECK_EQUAL(UINT32_MAX, r0);

    auto r1 = sc_tt_track(&t, 0);
    CHECK_EQUAL((UINT64_C(1) << 32), r1);

    auto r3 = sc_tt_track(&t, UINT32_MAX);
    CHECK_EQUAL(UINT32_MAX, r3);

    auto r4 = sc_tt_track(&t, UINT32_MAX - 199);
    CHECK_EQUAL(UINT32_MAX - 199, r4);

    auto r5 = sc_tt_track(&t, 4949);
    CHECK_EQUAL((UINT64_C(1) << 32) + 4949, r5);


}


} // anon
