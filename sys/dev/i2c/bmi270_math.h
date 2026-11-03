/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _DEV_I2C_BMI270_MATH_H_
#define _DEV_I2C_BMI270_MATH_H_

#include <sys/types.h>

int32_t bmi270_accel_um_s2(int16_t);
int32_t bmi270_gyro_mdps(int16_t);

#endif /* _DEV_I2C_BMI270_MATH_H_ */
