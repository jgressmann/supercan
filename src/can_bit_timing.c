/*
 * The MIT License (MIT)
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
 *
 */

#include "can_bit_timing.h"

#include <assert.h>

#if defined(_MSC_VER)
#	define inline __forceinline
#endif

static
inline
int
cbt_validate_range(uint32_t mi, uint32_t ma)
{
	if (ma < mi) {
		return CAN_BTRE_RANGE;
	}

	return CAN_BTRE_NONE;
}

static
inline
int
cbt_validate_hw_constraints(struct can_bit_timing_hw_contraints const *hw)
{
	int error = 0;
	uint32_t range = 0;

	if (!hw) {
		return CAN_BTRE_PARAM;
	}

	error = cbt_validate_range(hw->brp_min, hw->brp_max);
	if (error) {
		return error;
	}

	if (!hw->brp_step) {
		return CAN_BTRE_PARAM;
	}

	if (!hw->brp_min) {
		return CAN_BTRE_PARAM;
	}

	// brp range must be evenly divideable by step
	range = hw->brp_max - hw->brp_min;
	if ((range / hw->brp_step) * hw->brp_step != range) {
		return CAN_BTRE_PARAM;
	}

	error = cbt_validate_range(hw->tseg1_min, hw->tseg1_max);
	if (error) {
		return error;
	}

	error = cbt_validate_range(hw->tseg2_min, hw->tseg2_max);
	if (error) {
		return error;
	}

	if (hw->sjw_max < 1) {
		return CAN_BTRE_RANGE;
	}

	if (hw->clock_hz < 1) {
		return CAN_BTRE_RANGE;
	}

	return CAN_BTRE_NONE;
}


static
inline
int
cbt_validate_user_constraints(
		struct can_bit_timing_hw_contraints const *hw,
		struct can_bit_timing_constraints_fixed const *user)
{
	if (!user) {
		return CAN_BTRE_PARAM;
	}

	if (user->sample_point == 0 || user->sample_point >= CAN_SAMPLE_POINT_SCALE) {
		return CAN_BTRE_RANGE;
	}

	if (CAN_SJW_TSEG2 == user->sjw) {

	} else if (user->sjw >= 1) {
		if ((uint32_t)user->sjw > hw->sjw_max) {
			return CAN_BTRE_RANGE;
		}
	} else {
		return CAN_BTRE_PARAM;
	}

	if (user->bitrate < 1) {
		return CAN_BTRE_RANGE;
	}

	return CAN_BTRE_NONE;
}


int
cbt_run(
		struct can_bit_timing_hw_contraints const *hw,
		struct can_bit_timing_constraints_fixed const *user,
		struct can_bit_timing_settings *settings)
{
	int error = CAN_BTRE_NO_SOLUTION;

	uint32_t best_score = CAN_SAMPLE_POINT_SCALE;
	uint32_t best_brp = 0;
	uint32_t best_sjw = 0;
	uint32_t best_tseg1 = 0;
	uint32_t best_tseg2 = 0;
	uint32_t brp = 0;

	assert(hw->brp_min >= 1);
	assert(user->sjw == CAN_SJW_TSEG2 || (user->sjw >= 1 && (uint32_t)user->sjw < hw->sjw_max));

	for (brp = hw->brp_min; brp < hw->brp_max; brp += hw->brp_step) {
		const uint32_t can_hz = hw->clock_hz / brp;
		const uint32_t tqs = can_hz / user->bitrate;
		uint32_t tseg2 = 0;
		uint32_t tseg1 = 0;
		uint32_t current_sample_point = 0;
		uint32_t current_score = 0;

		if (user->min_tqs > 0 && tqs < (uint32_t)user->min_tqs) {
			break; // insuffient tqs, will only get worse as brp increases
		}

		if (tqs < 1 + hw->tseg1_min + hw->tseg2_min) {
			break; // insuffient tqs, will only get worse as brp increases
		}

		if (tqs > 1 + hw->tseg1_max + hw->tseg2_max) {
			continue; // this won't work
		}

		tseg2 = ((CAN_SAMPLE_POINT_SCALE - user->sample_point) * tqs + CAN_SAMPLE_POINT_SCALE / 2) / CAN_SAMPLE_POINT_SCALE;
		if (tseg2 < hw->tseg2_min) {
			tseg2 = hw->tseg2_min;
		} else if (tseg2 > hw->tseg2_max) {
			tseg2 = hw->tseg2_max;
			if (tseg2 + 3 > tqs) { // out of range
				continue;
			}
		}

		tseg1 = tqs - 1 - tseg2;
		if (tseg1 < hw->tseg1_min || tseg1 > hw->tseg1_max) {
			continue; // won't work unless we try to move tseg2 off ideal
		}

		current_sample_point = ((1 + tseg1) * CAN_SAMPLE_POINT_SCALE) / tqs;
		current_score = current_sample_point <= user->sample_point
				? user->sample_point - current_sample_point
				: current_sample_point - user->sample_point;

		if (CAN_BTRE_NO_SOLUTION == error || current_score < best_score) {
			error = CAN_BTRE_NONE;
			best_score = current_score;
			best_brp = brp;
			best_tseg1 = tseg1;
			best_tseg2 = tseg2;
			if (user->sjw == CAN_SJW_TSEG2) {
				best_sjw = tseg2;
				if (best_sjw > hw->sjw_max) {
					best_sjw = hw->sjw_max;
				}
			} else {
				best_sjw = user->sjw;
			}

			if (0 == current_score) {
				// best possible score
				break;
			}
		}
	}

	if (CAN_BTRE_NONE == error) {
		settings->brp = best_brp;
		settings->sjw = best_sjw;
		settings->tseg1 = best_tseg1;
		settings->tseg2 = best_tseg2;
	}

	return error;
}

int
cbt_fixed(
		struct can_bit_timing_hw_contraints const *hw,
		struct can_bit_timing_constraints_fixed const *user,
		struct can_bit_timing_settings *settings)
{
	int error = CAN_BTRE_NONE;

	error = cbt_validate_hw_constraints(hw);
	if (error) {
		return error;
	}

	error = cbt_validate_user_constraints(hw, user);
	if (error) {
		return error;
	}

	if (!settings) {
		return CAN_BTRE_PARAM;
	}

	return cbt_run(hw, user, settings);
}

int
cbt_real(
		struct can_bit_timing_hw_contraints const *hw,
		struct can_bit_timing_constraints_real const *user,
		struct can_bit_timing_settings *settings)
{
	struct can_bit_timing_constraints_fixed f;

	if (!user) {
		return CAN_BTRE_PARAM;
	}

	if (user->sample_point < 0 || user->sample_point > 1) {
		return CAN_BTRE_RANGE;
	}

	f.sjw = user->sjw;
	f.bitrate = user->bitrate;
	f.min_tqs = user->min_tqs;
	f.sample_point = (uint16_t)(user->sample_point * CAN_SAMPLE_POINT_SCALE);
	return cbt_fixed(hw, &f, settings);
}

