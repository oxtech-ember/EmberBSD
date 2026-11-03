/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>

#include "../bmi270_math.h"

int
main(void)
{
	assert(bmi270_accel_um_s2(0) == 0);
	assert(bmi270_accel_um_s2(1) == 599);
	assert(bmi270_accel_um_s2(-1) == -599);
	assert(bmi270_accel_um_s2(16384) == 9806650);
	assert(bmi270_accel_um_s2(-16384) == -9806650);
	assert(bmi270_accel_um_s2(32767) == 19612701);
	assert(bmi270_accel_um_s2(-32768) == -19613300);
	assert(bmi270_gyro_mdps(0) == 0);
	assert(bmi270_gyro_mdps(1) == 61);
	assert(bmi270_gyro_mdps(-1) == -61);
	assert(bmi270_gyro_mdps(16384) == 1000000);
	assert(bmi270_gyro_mdps(-16384) == -1000000);
	assert(bmi270_gyro_mdps(32767) == 1999939);
	assert(bmi270_gyro_mdps(-32768) == -2000000);
	return 0;
}
