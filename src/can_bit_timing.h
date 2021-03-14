/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020-2021 Jean Gressmann <jean@0x42.de>
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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct can_bit_timing_settings {
	uint32_t brp;
	uint32_t tseg1;
	uint32_t tseg2;
	uint32_t sjw;
};

struct can_bit_timing_hw_contraints {
	uint32_t clock_hz;
	uint32_t brp_min;
	uint32_t brp_max;
	uint32_t brp_step;
	uint32_t tseg1_min;
	uint32_t tseg1_max;
	uint32_t tseg2_min;
	uint32_t tseg2_max;
	uint32_t sjw_max;
};

enum {
	CAN_SAMPLE_POINT_SCALE = 1024
};

enum {
	CAN_SJW_TSEG2 = 0
};

enum {
	CAN_BTRE_NO_SOLUTION = 1,
	CAN_BTRE_NONE = 0,
	CAN_BTRE_PARAM = -1,
	CAN_BTRE_RANGE = -2,
	CAN_BTRE_UNKNOWN = -3,
};

struct can_bit_timing_constraints_real {
	float sample_point; // [0-1]
	uint32_t bitrate;   // [bps]
	int sjw;
	int min_tqs;
};

struct can_bit_timing_constraints_fixed {
	uint32_t sample_point; // [0-1024]
	uint32_t bitrate;      // [bps]
	int sjw;
	int min_tqs;
};

/* fix point math computation of can bit timing */
int
cbt_fixed(
	struct can_bit_timing_hw_contraints const *hw,
	struct can_bit_timing_constraints_fixed const *user,
	struct can_bit_timing_settings *settings);

/* floating point version */
int
cbt_real(
	struct can_bit_timing_hw_contraints const *hw,
	struct can_bit_timing_constraints_real const *user,
	struct can_bit_timing_settings *settings);




void cia_classic_cbt_init_default_fixed(
	struct can_bit_timing_constraints_fixed *user);

void cia_classic_cbt_init_default_real(
	struct can_bit_timing_constraints_real *user);

void cia_fd_cbt_init_default_fixed(
	struct can_bit_timing_constraints_fixed *user_nominal,
	struct can_bit_timing_constraints_fixed *user_data);

void cia_fd_cbt_init_default_real(
	struct can_bit_timing_constraints_real *user_nominal,
	struct can_bit_timing_constraints_real *user_data);


int
cia_classic_cbt_fixed(
	struct can_bit_timing_hw_contraints const *hw,
	struct can_bit_timing_constraints_fixed const *user,
	struct can_bit_timing_settings *settings);

/* floating point version */
int
cia_classic_cbt_real(
	struct can_bit_timing_hw_contraints const *hw,
	struct can_bit_timing_constraints_real const *user,
	struct can_bit_timing_settings *settings);


/* Computes CAN-FD bit timing according to CiA recommendations
 *
 * https://can-newsletter.org/uploads/media/raw/f6a36d1461371a2f86ef0011a513712c.pdf
 *
 * R1: highest clock frequency
 * R2: same prescaler for arbitration & data
 * R3: choose the lowest bitrate prescaler (brp) possible
 * R4: configure all nodes to have the same SP
 * R5: choose sjw as large as possible
 * R6: enable transmitter delay compensation for data bitrates >= 1MBit/s
 */
int
cia_fd_cbt_fixed(
	struct can_bit_timing_hw_contraints const *hw_nominal,
	struct can_bit_timing_hw_contraints const *hw_data,
	struct can_bit_timing_constraints_fixed const *user_nominal,
	struct can_bit_timing_constraints_fixed const *user_data,
	struct can_bit_timing_settings *settings_nominal,
	struct can_bit_timing_settings *settings_data);

/* floating point version */
int
cia_fd_cbt_real(
	struct can_bit_timing_hw_contraints const *hw_nominal,
	struct can_bit_timing_hw_contraints const *hw_data,
	struct can_bit_timing_constraints_real const *user_nominal,
	struct can_bit_timing_constraints_real const *user_data,
	struct can_bit_timing_settings *settings_nominal,
	struct can_bit_timing_settings *settings_data);



#ifdef __cplusplus
}
#endif
