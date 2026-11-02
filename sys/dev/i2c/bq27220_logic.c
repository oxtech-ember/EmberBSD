/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <sys/cdefs.h>
#ifdef _KERNEL
__KERNEL_RCSID(0, "$NetBSD$");
#endif

#ifdef _KERNEL
#include <sys/param.h>
#else
#include <limits.h>
#include <stddef.h>
#endif

#include "bq27220_logic.h"

uint16_t
bq27220_decode_word(const uint8_t bytes[2])
{
	if (bytes == NULL)
		return 0;

	return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

bool
bq27220_sensor_value(enum bq27220_sensor sensor, uint16_t raw,
    int32_t *value)
{
	int32_t current;

	if (value == NULL)
		return false;

	switch (sensor) {
	case BQ27220_SENSOR_VOLTAGE:
		/* Voltage() is returned in millivolts; envsys uses microvolts. */
		if (raw > 6000)
			return false;
		*value = (int32_t)raw * 1000;
		return true;
	case BQ27220_SENSOR_CURRENT:
		/* Current() is signed milliamps, positive into the battery. */
		current = (raw & 0x8000) ? (int32_t)raw - 65536 : raw;
		*value = current * 1000;
		return true;
	case BQ27220_SENSOR_TEMPERATURE:
		/* Temperature() is 0.1 K; reject values that overflow envsys. */
		if (raw > INT32_MAX / 100000)
			return false;
		*value = (int32_t)raw * 100000;
		return true;
	case BQ27220_SENSOR_SOC:
		if (raw > 100)
			return false;
		*value = raw;
		return true;
	default:
		return false;
	}
}
