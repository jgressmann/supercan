#include <CppUnitLite2.h>
#include <limits>
#include <cstring>
#include <cassert>

#include "usnprintf.h"
#include "can_bit_timing.h"

using namespace std;



namespace
{

struct fixture
{
	can_bit_timing_hw_contraints hw;
	can_bit_timing_constraints_real user;
	can_bit_timing_settings settings;


#define M_CAN_NMBT_TQ_MIN			0x0004
#define M_CAN_NMBT_TQ_MAX			0x0181
#define M_CAN_NMBT_BRP_MIN		   0x0001
#define M_CAN_NMBT_BRP_MAX		   0x0200
#define M_CAN_NMBT_SJW_MIN		   0x0001
#define M_CAN_NMBT_SJW_MAX		   0x0080
#define M_CAN_NMBT_TSEG1_MIN		 0x0002
#define M_CAN_NMBT_TSEG1_MAX		 0x0100
#define M_CAN_NMBT_TSEG2_MIN		 0x0002
#define M_CAN_NMBT_TSEG2_MAX		 0x0080

#define M_CAN_DTBT_TQ_MIN			0x04
#define M_CAN_DTBT_TQ_MAX			0x31
#define M_CAN_DTBT_BRP_MIN		   0x01
#define M_CAN_DTBT_BRP_MAX		   0x20
#define M_CAN_DTBT_SJW_MIN		   0x01
#define M_CAN_DTBT_SJW_MAX		   0x10
#define M_CAN_DTBT_TSEG1_MIN		 0x01
#define M_CAN_DTBT_TSEG1_MAX		 0x20
#define M_CAN_DTBT_TSEG2_MIN		 0x01
#define M_CAN_DTBT_TSEG2_MAX		 0x10
#define M_CAN_TDCR_TDCO_MAX		  0x7f


	fixture()
	{
		set_m_can_nominal_at_80_mhz();

		user.sjw = 1;
		user.bitrate = 500000;
		user.sample_point = .8f;
		user.min_tqs = 0;
	}

	void set_m_can_nominal_at_80_mhz()
	{
		hw.brp_min = 1;
		hw.brp_max = 0x0200;
		hw.brp_step = 1;
		hw.sjw_max = 0x0080;
		hw.tseg1_min = 0x0002;
		hw.tseg1_max = 0x0100;
		hw.tseg2_min = 0x0002;
		hw.tseg2_max = 0x0080;
		hw.clock_hz = UINT32_C(80000000);
	}

	void set_m_can_data_at_80_mhz()
	{
		hw.brp_min = 1;
		hw.brp_max = 0x20;
		hw.brp_step = 1;
		hw.sjw_max = 0x10;
		hw.tseg1_min = 0x01;
		hw.tseg1_max = 0x20;
		hw.tseg2_min = 0x01;
		hw.tseg2_max = 0x10;
		hw.clock_hz = UINT32_C(80000000);
	}
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

TEST_F (fixture, cbt_handles_hw_invalid_params_properly)
{
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
	set_m_can_nominal_at_80_mhz();

	user.bitrate = 500000;
	user.sjw = 1;
	user.sample_point = .8f;
	user.min_tqs = 0;

	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(1, settings.brp);
	CHECK_EQUAL(1, settings.sjw);
	CHECK_EQUAL(127, settings.tseg1);
	CHECK_EQUAL(32, settings.tseg2);
	//	nominal brp=1 sjw=1 tseg1=127 tseg2=32 bitrate=500000 sp=798/1000

	user.bitrate = 1000000;
	user.sjw = 1;
	user.sample_point = .8f;
	user.min_tqs = 0;

	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(1, settings.brp);
	CHECK_EQUAL(1, settings.sjw);
	CHECK_EQUAL(63, settings.tseg1);
	CHECK_EQUAL(16, settings.tseg2);
	// nominal brp=1 sjw=1 tseg1=63 tseg2=16 bitrate=1000000 sp=797/1000

	// data
	set_m_can_data_at_80_mhz();

	user.bitrate = 2000000;
	user.sjw = 1;
	user.sample_point = .7f;
	user.min_tqs = 0;

	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(1, settings.brp);
	CHECK_EQUAL(1, settings.sjw);
	CHECK_EQUAL(27, settings.tseg1);
	CHECK_EQUAL(12, settings.tseg2);
	// data brp=1 sjw=1 tseg1=27 tseg2=12 bitrate=2000000 sp=692/1000

	user.bitrate = 4000000;
	user.sjw = 1;
	user.sample_point = .7f;
	user.min_tqs = 0;
	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(1, settings.brp);
	CHECK_EQUAL(1, settings.sjw);
	CHECK_EQUAL(13, settings.tseg1);
	CHECK_EQUAL(6, settings.tseg2);
	// data brp=1 sjw=1 tseg1=13 tseg2=6 bitrate=4000000 sp=684/1000

	user.bitrate = 5000000;
	user.sjw = 1;
	user.sample_point = .7f;
	user.min_tqs = 0;
	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(1, settings.brp);
	CHECK_EQUAL(1, settings.sjw);
	CHECK_EQUAL(10, settings.tseg1);
	CHECK_EQUAL(5, settings.tseg2);
	// data brp=1 sjw=1 tseg1=10 tseg2=5 bitrate=5000000 sp=666/1000

	user.bitrate = 8000000;
	user.sjw = 1;
	user.sample_point = .7f;
	user.min_tqs = 0;
	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(1, settings.brp);
	CHECK_EQUAL(1, settings.sjw);
	CHECK_EQUAL(6, settings.tseg1);
	CHECK_EQUAL(3, settings.tseg2);
	// data brp=1 sjw=1 tseg1=6 tseg2=3 bitrate=8000000 sp=666/1000



	user.bitrate = 8000000;
	user.sjw = CAN_SJW_TSEG2;
	user.sample_point = .7f;
	user.min_tqs = 0;
	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(1, settings.brp);
	CHECK_EQUAL(3, settings.sjw);
	CHECK_EQUAL(6, settings.tseg1);
	CHECK_EQUAL(3, settings.tseg2);
	// data brp=1 sjw=1 tseg1=6 tseg2=3 bitrate=8000000 sp=666/1000
}


TEST_F (fixture, cbt_computes_sets_sjw_to_tseg2)
{
	set_m_can_nominal_at_80_mhz();

	user.bitrate = 500000;
	user.sjw = CAN_SJW_TSEG2;
	user.sample_point = .8f;
	user.min_tqs = 0;

	CHECK_EQUAL(CAN_BTRE_NONE, cbt_real(&hw, &user, &settings));
	CHECK_EQUAL(32, settings.sjw);
	CHECK_EQUAL(32, settings.tseg2);
}

TEST_F (fixture, cbt_yields_no_result_if_tqs_requirement_cannot_be_met)
{
	set_m_can_data_at_80_mhz();
	// 8Mhz clock
	hw.clock_hz /= 10;

	user.bitrate = 5000000;
	user.sjw = CAN_SJW_TSEG2;
	user.sample_point = .75f;
	user.min_tqs = 16;

	CHECK_EQUAL(CAN_BTRE_NO_SOLUTION, cbt_real(&hw, &user, &settings));
}


} // anon namespace
