/* SPDX-License-Identifier: BSD-2-Clause */

#ifdef _KERNEL
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#endif

#include <sys/types.h>

#include "bmi270_math.h"

#define BMI270_RAW_FULL_SCALE	32768LL

static int32_t
bmi270_scale(int16_t raw, int64_t full_scale)
{
	int64_t scaled = (int64_t)raw * full_scale;
	int64_t half = BMI270_RAW_FULL_SCALE / 2;

	if (scaled < 0)
		return (int32_t)-((-scaled + half) / BMI270_RAW_FULL_SCALE);
	return (int32_t)((scaled + half) / BMI270_RAW_FULL_SCALE);
}

int32_t
bmi270_accel_um_s2(int16_t raw)
{
	/* 2 g full scale; 9.80665 m/s^2 per g. */
	return bmi270_scale(raw, 19613300LL);
}

int32_t
bmi270_gyro_mdps(int16_t raw)
{
	/* 2000 degrees/s full scale, expressed in millidegrees/s. */
	return bmi270_scale(raw, 2000000LL);
}
