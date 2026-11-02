/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _DEV_I2C_BQ27220_LOGIC_H_
#define _DEV_I2C_BQ27220_LOGIC_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

enum bq27220_sensor {
	BQ27220_SENSOR_VOLTAGE,
	BQ27220_SENSOR_CURRENT,
	BQ27220_SENSOR_TEMPERATURE,
	BQ27220_SENSOR_SOC,
	BQ27220_NSENSORS
};

uint16_t bq27220_decode_word(const uint8_t [2]);
bool bq27220_sensor_value(enum bq27220_sensor, uint16_t, int32_t *);

#endif /* _DEV_I2C_BQ27220_LOGIC_H_ */
