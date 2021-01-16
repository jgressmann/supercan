#include <CppUnitLite2.h>
#include <limits>
#include <cstring>
#include <cassert>

#include "usnprintf.h"
#include "can_bit_timing.h"

using namespace std;


#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
namespace
{

struct fixture
{
    can_bit_timing_hw_contraints hw_nominal, hw_data;
    can_bit_timing_constraints_real user_nominal, user_data;
    can_bit_timing_settings settings_nominal, settings_data;


#define M_CAN_NMBT_TQ_MIN                        0x0004
#define M_CAN_NMBT_TQ_MAX                        0x0181
#define M_CAN_NMBT_BRP_MIN                   0x0001
#define M_CAN_NMBT_BRP_MAX                   0x0200
#define M_CAN_NMBT_SJW_MIN                   0x0001
#define M_CAN_NMBT_SJW_MAX                   0x0080
#define M_CAN_NMBT_TSEG1_MIN                 0x0002
#define M_CAN_NMBT_TSEG1_MAX                 0x0100
#define M_CAN_NMBT_TSEG2_MIN                 0x0002
#define M_CAN_NMBT_TSEG2_MAX                 0x0080

#define M_CAN_DTBT_TQ_MIN                        0x04
#define M_CAN_DTBT_TQ_MAX                        0x31
#define M_CAN_DTBT_BRP_MIN                   0x01
#define M_CAN_DTBT_BRP_MAX                   0x20
#define M_CAN_DTBT_SJW_MIN                   0x01
#define M_CAN_DTBT_SJW_MAX                   0x10
#define M_CAN_DTBT_TSEG1_MIN                 0x01
#define M_CAN_DTBT_TSEG1_MAX                 0x20
#define M_CAN_DTBT_TSEG2_MIN                 0x01
#define M_CAN_DTBT_TSEG2_MAX                 0x10
#define M_CAN_TDCR_TDCO_MAX                  0x7f


    fixture()
    {
        hw_nominal.brp_min = 1;
        hw_nominal.brp_max = 0x0200;
        hw_nominal.brp_step = 1;
        hw_nominal.sjw_max = 0x0080;
        hw_nominal.tseg1_min = 0x0002;
        hw_nominal.tseg1_max = 0x0100;
        hw_nominal.tseg2_min = 0x0002;
        hw_nominal.tseg2_max = 0x0080;
        hw_nominal.clock_hz = INT32_C(80000000);

        hw_data.brp_min = 1;
        hw_data.brp_max = 0x20;
        hw_data.brp_step = 1;
        hw_data.sjw_max = 0x10;
        hw_data.tseg1_min = 0x01;
        hw_data.tseg1_max = 0x20;
        hw_data.tseg2_min = 0x01;
        hw_data.tseg2_max = 0x10;
        hw_data.clock_hz = UINT32_C(80000000);

        user_nominal.sjw = 1;
        user_nominal.bitrate = 500000;
        user_nominal.sample_point = .8f;
        user_nominal.min_tqs = 0;

        user_data.sjw = 1;
        user_data.bitrate = 2000000;
        user_data.sample_point = .7f;
        user_data.min_tqs = 0;
    }



    // void set_m_can_nominal_at_80_mhz()
    // {
    //     hw.brp_min = 1;
    //     hw.brp_max = 0x0200;
    //     hw.brp_step = 1;
    //     hw.sjw_max = 0x0080;
    //     hw.tseg1_min = 0x0002;
    //     hw.tseg1_max = 0x0100;
    //     hw.tseg2_min = 0x0002;
    //     hw.tseg2_max = 0x0080;
    //     hw.clock_hz = UINT32_C(80000000);
    // }

    // void set_m_can_data_at_80_mhz()
    // {
    //     hw.brp_min = 1;
    //     hw.brp_max = 0x20;
    //     hw.brp_step = 1;
    //     hw.sjw_max = 0x10;
    //     hw.tseg1_min = 0x01;
    //     hw.tseg1_max = 0x20;
    //     hw.tseg2_min = 0x01;
    //     hw.tseg2_max = 0x10;
    //     hw.clock_hz = UINT32_C(80000000);
    // }
};

TEST (cbt_handles_null_pointer_properly)
{
    can_bit_timing_hw_contraints hw;
    can_bit_timing_constraints_real user;
    user.sample_point = .6f;
    can_bit_timing_settings settings;
    CHECK(0 > cbt_real(NULL, &user, &settings));
    CHECK_EQUAL(CAN_BTRE_PARAM, cbt_real(&hw, NULL, &settings));
    CHECK_EQUAL(CAN_BTRE_PARAM, cbt_real(&hw, &user, NULL));
}


TEST (cia_classic_cbt_init_default_fixed)
{
    uint32_t nominal_bitrates[] = {
        125000,
        250000,
        500000,
        800000,
        1000000
    };

    uint32_t nominal_sample_points[] = {
        896,
        896,
        896,
        820,
        768
    };

    for (size_t i = 0; i < ARRAY_SIZE(nominal_bitrates); ++i) {
        can_bit_timing_constraints_fixed user;
        memset(&user, 0, sizeof(user));
        user.bitrate = nominal_bitrates[i];
        cia_classic_cbt_init_default_fixed(&user);
        CHECK_EQUAL(nominal_bitrates[i], user.bitrate);
        CHECK_EQUAL(nominal_sample_points[i], user.sample_point);
        CHECK_EQUAL(CAN_SJW_TSEG2, user.sjw);
    }
}

TEST (cia_classic_cbt_init_default_real)
{
    uint32_t nominal_bitrates[] = {
        125000,
        250000,
        500000,
        800000,
        1000000
    };

    float nominal_sample_points[] = {
        .875f,
        .875f,
        .875f,
        .8f,
        .75f
    };

    for (size_t i = 0; i < ARRAY_SIZE(nominal_bitrates); ++i) {
        can_bit_timing_constraints_real user;
        memset(&user, 0, sizeof(user));
        user.bitrate = nominal_bitrates[i];
        cia_classic_cbt_init_default_real(&user);
        CHECK_EQUAL(nominal_bitrates[i], user.bitrate);
        CHECK_CLOSE(nominal_sample_points[i], user.sample_point, 1e-6f);
        CHECK_EQUAL(CAN_SJW_TSEG2, user.sjw);
    }
}

TEST (cia_fd_cbt_init_default_fixed)
{
    uint32_t nominal_bitrates[] = {
        125000,
        250000,
        500000,
        800000,
        1000000
    };

    uint32_t nominal_sample_points[] = {
        896,
        896,
        896,
        820,
        768
    };

    uint32_t data_bitrates[] = {
        500000,
        1000000,
        2000000,
        5000000,
        8000000
    };

    uint32_t data_sample_points[] = {
        768,
        768,
        768,
        768,
        717
    };

    for (size_t i = 0; i < ARRAY_SIZE(nominal_bitrates); ++i) {
        can_bit_timing_constraints_fixed user_nominal;
        memset(&user_nominal, 0, sizeof(user_nominal));
        user_nominal.bitrate = nominal_bitrates[i];

        for (size_t j = 0; j < ARRAY_SIZE(data_bitrates); ++j) {
            can_bit_timing_constraints_fixed user_data;
            memset(&user_data, 0, sizeof(user_data));
            user_data.bitrate = data_bitrates[j];

            cia_fd_cbt_init_default_fixed(&user_nominal, &user_data);
            CHECK_EQUAL(nominal_bitrates[i], user_nominal.bitrate);
            CHECK_EQUAL(nominal_sample_points[i], user_nominal.sample_point);
            CHECK_EQUAL(CAN_SJW_TSEG2, user_nominal.sjw);
            CHECK_EQUAL(data_bitrates[j], user_data.bitrate);
            CHECK_EQUAL(data_sample_points[j], user_data.sample_point);
            CHECK_EQUAL(CAN_SJW_TSEG2, user_data.sjw);
        }
    }
}


TEST (cia_fd_cbt_init_default_real)
{
    uint32_t nominal_bitrates[] = {
        125000,
        250000,
        500000,
        800000,
        1000000
    };

    float nominal_sample_points[] = {
        .875f,
        .875f,
        .875f,
        .8f,
        .75f
    };

    uint32_t data_bitrates[] = {
        500000,
        1000000,
        2000000,
        5000000,
        8000000
    };

    float data_sample_points[] = {
        .75f,
        .75f,
        .75f,
        .75f,
        .7f
    };

    for (size_t i = 0; i < ARRAY_SIZE(nominal_bitrates); ++i) {
        can_bit_timing_constraints_real user_nominal;
        memset(&user_nominal, 0, sizeof(user_nominal));
        user_nominal.bitrate = nominal_bitrates[i];

        for (size_t j = 0; j < ARRAY_SIZE(data_bitrates); ++j) {
            can_bit_timing_constraints_real user_data;
            memset(&user_data, 0, sizeof(user_data));
            user_data.bitrate = data_bitrates[j];

            cia_fd_cbt_init_default_real(&user_nominal, &user_data);
            CHECK_EQUAL(nominal_bitrates[i], user_nominal.bitrate);
            CHECK_CLOSE(nominal_sample_points[i], user_nominal.sample_point, 1e-6f);
            CHECK_EQUAL(CAN_SJW_TSEG2, user_nominal.sjw);
            CHECK_EQUAL(data_bitrates[j], user_data.bitrate);
            CHECK_CLOSE(data_sample_points[j], user_data.sample_point, 1e-6f);
            CHECK_EQUAL(CAN_SJW_TSEG2, user_data.sjw);
        }
    }
}

TEST_F (fixture, cbt_handles_hw_invalid_params_properly)
{
    auto& hw = hw_nominal;
    auto& user = user_nominal;
    auto& settings = settings_nominal;
    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));

    hw.brp_max = 0;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.brp_max = 100;
    hw.brp_min = 200;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.brp_max = 512;
    hw.brp_min = 1;
    hw.brp_step = 3;
    CHECK_EQUAL(CAN_BTRE_PARAM, cbt_real(&hw, &user, &settings));
    hw.brp_step = 1;
    hw.brp_min = 0; // invalid
    CHECK_EQUAL(CAN_BTRE_PARAM, cbt_real(&hw, &user, &settings));
    hw.brp_min = 1;
    hw.sjw_max = 0;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.sjw_max = 100;
    hw.tseg1_min = 300;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.tseg1_min = 1;
    hw.tseg1_max = 0;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.tseg1_min = 1;
    hw.tseg1_max = 300;
    hw.tseg2_min = 300;
    hw.tseg2_max = 100;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.tseg2_min = 100;
    hw.tseg2_max = 0;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.tseg2_min = 1;
    hw.tseg2_max = 120;
    hw.clock_hz = 0;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    hw.clock_hz = 80000000;

    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
}

TEST_F (fixture, cbt_handles_user_invalid_params_properly)
{
    auto& hw = hw_nominal;
    auto& user = user_nominal;
    auto& settings = settings_nominal;
    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));

    user.sjw = -1;
    CHECK_EQUAL(CAN_BTRE_PARAM, cbt_real(&hw, &user, &settings));
    user.sjw = 1000;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    user.sjw = 1;
    user.sample_point = -1;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    user.sample_point = 2;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    user.sample_point = .6f;
    user.bitrate = 0;
    CHECK_EQUAL(CAN_BTRE_RANGE, cbt_real(&hw, &user, &settings));
    user.bitrate = 2000000;

    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
}


TEST_F (fixture, cbt_computes_sensible_values)
{
    user_nominal.bitrate = 500000;
    user_nominal.sjw = 1;
    user_nominal.sample_point = .8f;
    user_nominal.min_tqs = 0;

    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_nominal, &user_nominal, &settings_nominal));
    CHECK_EQUAL(1, settings_nominal.brp);
    CHECK_EQUAL(1, settings_nominal.sjw);
    CHECK_EQUAL(127, settings_nominal.tseg1);
    CHECK_EQUAL(32, settings_nominal.tseg2);
    //        nominal brp=1 sjw=1 tseg1=127 tseg2=32 bitrate=500000 sp=798/1000

    user_nominal.bitrate = 1000000;
    user_nominal.sjw = 1;
    user_nominal.sample_point = .8f;
    user_nominal.min_tqs = 0;

    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_nominal, &user_nominal, &settings_nominal));
    CHECK_EQUAL(1, settings_nominal.brp);
    CHECK_EQUAL(1, settings_nominal.sjw);
    CHECK_EQUAL(63, settings_nominal.tseg1);
    CHECK_EQUAL(16, settings_nominal.tseg2);
    // nominal brp=1 sjw=1 tseg1=63 tseg2=16 bitrate=1000000 sp=797/1000

    // data


    user_data.bitrate = 2000000;
    user_data.sjw = 1;
    user_data.sample_point = .7f;
    user_data.min_tqs = 0;

    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_data, &user_data, &settings_data));
    CHECK_EQUAL(1, settings_data.brp);
    CHECK_EQUAL(1, settings_data.sjw);
    CHECK_EQUAL(27, settings_data.tseg1);
    CHECK_EQUAL(12, settings_data.tseg2);
    // data brp=1 sjw=1 tseg1=27 tseg2=12 bitrate=2000000 sp=692/1000

    user_data.bitrate = 4000000;
    user_data.sjw = 1;
    user_data.sample_point = .7f;
    user_data.min_tqs = 0;
    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_data, &user_data, &settings_data));
    CHECK_EQUAL(1, settings_data.brp);
    CHECK_EQUAL(1, settings_data.sjw);
    CHECK_EQUAL(13, settings_data.tseg1);
    CHECK_EQUAL(6, settings_data.tseg2);
    // data brp=1 sjw=1 tseg1=13 tseg2=6 bitrate=4000000 sp=684/1000

    user_data.bitrate = 5000000;
    user_data.sjw = 1;
    user_data.sample_point = .7f;
    user_data.min_tqs = 0;
    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_data, &user_data, &settings_data));
    CHECK_EQUAL(1, settings_data.brp);
    CHECK_EQUAL(1, settings_data.sjw);
    CHECK_EQUAL(10, settings_data.tseg1);
    CHECK_EQUAL(5, settings_data.tseg2);
    // data brp=1 sjw=1 tseg1=10 tseg2=5 bitrate=5000000 sp=666/1000

    user_data.bitrate = 8000000;
    user_data.sjw = 1;
    user_data.sample_point = .7f;
    user_data.min_tqs = 0;
    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_data, &user_data, &settings_data));
    CHECK_EQUAL(1, settings_data.brp);
    CHECK_EQUAL(1, settings_data.sjw);
    CHECK_EQUAL(6, settings_data.tseg1);
    CHECK_EQUAL(3, settings_data.tseg2);
    // data brp=1 sjw=1 tseg1=6 tseg2=3 bitrate=8000000 sp=666/1000



    user_data.bitrate = 8000000;
    user_data.sjw = CAN_SJW_TSEG2;
    user_data.sample_point = .7f;
    user_data.min_tqs = 0;
    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_data, &user_data, &settings_data));
    CHECK_EQUAL(1, settings_data.brp);
    CHECK_EQUAL(3, settings_data.sjw);
    CHECK_EQUAL(6, settings_data.tseg1);
    CHECK_EQUAL(3, settings_data.tseg2);
    // data brp=1 sjw=1 tseg1=6 tseg2=3 bitrate=8000000 sp=666/1000
}


TEST_F (fixture, cbt_computes_sets_sjw_to_tseg2)
{
    user_nominal.bitrate = 500000;
    user_nominal.sjw = CAN_SJW_TSEG2;
    user_nominal.sample_point = .8f;
    user_nominal.min_tqs = 0;

    CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw_nominal, &user_nominal, &settings_nominal));
    CHECK_EQUAL(32, settings_nominal.sjw);
    CHECK_EQUAL(32, settings_nominal.tseg2);
}

TEST_F (fixture, cbt_yields_no_result_if_tqs_requirement_cannot_be_met)
{
    // 8Mhz clock
    hw_data.clock_hz = 8000000;

    user_data.bitrate = 5000000;
    user_data.sjw = CAN_SJW_TSEG2;
    user_data.sample_point = .75f;
    user_data.min_tqs = 16;

    CHECK_EQUAL(CAN_BTRE_NO_SOLUTION, cbt_real(&hw_data, &user_data, &settings_data));
}

TEST_F (fixture, cia_classic_cbt_uses_lowest_brp_if_possible)
{
    user_nominal.bitrate = 500000;
    user_nominal.sjw = 1;
    user_nominal.sample_point = .8f;
    user_nominal.min_tqs = 0;

    CHECK_EQUAL(CAN_BTRE_NONE, cia_classic_cbt_real(&hw_nominal, &user_nominal, &settings_nominal));
    CHECK_EQUAL(1, settings_nominal.brp);
    CHECK_EQUAL(32, settings_nominal.sjw);
    CHECK_EQUAL(127, settings_nominal.tseg1);
    CHECK_EQUAL(32, settings_nominal.tseg2);

    user_nominal.bitrate = 500000;
    user_nominal.sjw = 1;
    user_nominal.sample_point = .7f;
    user_nominal.min_tqs = 0;

    CHECK_EQUAL(CAN_BTRE_NONE, cia_classic_cbt_real(&hw_nominal, &user_nominal, &settings_nominal));
    CHECK_EQUAL(1, settings_nominal.brp);
    CHECK_EQUAL(48, settings_nominal.sjw);
    CHECK_EQUAL(111, settings_nominal.tseg1);
    CHECK_EQUAL(48, settings_nominal.tseg2);
    // nominal brp=1 sjw=1 tseg1=111 tseg2=48 bitrate=500000 sp=700/1000

    // force higher brp
    hw_nominal.clock_hz *= 4;
    CHECK_EQUAL(CAN_BTRE_NONE, cia_classic_cbt_real(&hw_nominal, &user_nominal, &settings_nominal));
    CHECK_EQUAL(2, settings_nominal.brp);
    CHECK_EQUAL(96, settings_nominal.sjw);
    CHECK_EQUAL(223, settings_nominal.tseg1);
    CHECK_EQUAL(96, settings_nominal.tseg2);
}

TEST_F (fixture, cia_fd_cbt_uses_lowest_brp_if_possible)
{
    user_nominal.bitrate = 500000;
    user_nominal.sjw = 1;
    user_nominal.sample_point = .8f;
    user_nominal.min_tqs = 0;

    user_data.bitrate = 2000000;
    user_data.sjw = 1;
    user_data.sample_point = .7f;
    user_data.min_tqs = 0;

    CHECK_EQUAL(CAN_BTRE_NONE, cia_fd_cbt_real(
        &hw_nominal, &hw_data,
        &user_nominal, &user_data,
        &settings_nominal, &settings_data));
    CHECK_EQUAL(1, settings_nominal.brp);
    CHECK_EQUAL(32, settings_nominal.sjw);
    CHECK_EQUAL(127, settings_nominal.tseg1);
    CHECK_EQUAL(32, settings_nominal.tseg2);

    CHECK_EQUAL(1, settings_data.brp);
    CHECK_EQUAL(12, settings_data.sjw);
    CHECK_EQUAL(27, settings_data.tseg1);
    CHECK_EQUAL(12, settings_data.tseg2);

    // 1/8 MBit/s
    user_nominal.bitrate = 1000000;
    user_nominal.sjw = 1;
    user_nominal.sample_point = .8f;
    user_nominal.min_tqs = 0;

    user_data.bitrate = 8000000;
    user_data.sjw = 1;
    user_data.sample_point = .7f;
    user_data.min_tqs = 0;
    CHECK_EQUAL(CAN_BTRE_NONE, cia_fd_cbt_real(
        &hw_nominal, &hw_data,
        &user_nominal, &user_data,
        &settings_nominal, &settings_data));

    CHECK_EQUAL(1, settings_nominal.brp);
    CHECK_EQUAL(16, settings_nominal.sjw);
    CHECK_EQUAL(63, settings_nominal.tseg1);
    CHECK_EQUAL(16, settings_nominal.tseg2);

    CHECK_EQUAL(1, settings_data.brp);
    CHECK_EQUAL(3, settings_data.sjw);
    CHECK_EQUAL(6, settings_data.tseg1);
    CHECK_EQUAL(3, settings_data.tseg2);

    // force higher brp
    hw_nominal.clock_hz *= 16;
    hw_data.clock_hz = hw_nominal.clock_hz;

    CHECK_EQUAL(CAN_BTRE_NONE, cia_fd_cbt_real(
        &hw_nominal, &hw_data,
        &user_nominal, &user_data,
        &settings_nominal, &settings_data));
    CHECK_EQUAL(4, settings_nominal.brp);
    CHECK_EQUAL(64, settings_nominal.sjw);
    CHECK_EQUAL(255, settings_nominal.tseg1);
    CHECK_EQUAL(64, settings_nominal.tseg2);

    CHECK_EQUAL(4, settings_data.brp);
    CHECK_EQUAL(12, settings_data.sjw);
    CHECK_EQUAL(27, settings_data.tseg1);
    CHECK_EQUAL(12, settings_data.tseg2);

// ch1 SC_MSG_NM_BITTIMING
// nominal brp=1 sjw=1 tseg1=127 tseg2=32 bitrate=500000 sp=800/1000
// ch1 SC_MSG_DT_BITTIMING
// data brp=1 sjw=1 tseg1=27 tseg2=12 bitrate=2000000 sp=700/1000

}


} // anon namespace
