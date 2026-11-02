/*
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "../bq27220_logic.h"

int
main(void)
{
	const uint8_t word[2] = { 0x96, 0x0b };
	int32_t value;

	assert(bq27220_decode_word(word) == 2966);
	assert(bq27220_decode_word(NULL) == 0);
	assert(bq27220_sensor_value(BQ27220_SENSOR_VOLTAGE, 3850, &value));
	assert(value == 3850000);
	assert(bq27220_sensor_value(BQ27220_SENSOR_CURRENT, 0xff83, &value));
	assert(value == -125000);
	assert(bq27220_sensor_value(BQ27220_SENSOR_CURRENT, 125, &value));
	assert(value == 125000);
	assert(bq27220_sensor_value(BQ27220_SENSOR_TEMPERATURE, 2982, &value));
	assert(value == 298200000);
	assert(bq27220_sensor_value(BQ27220_SENSOR_SOC, 0, &value));
	assert(value == 0);
	assert(bq27220_sensor_value(BQ27220_SENSOR_SOC, 100, &value));
	assert(value == 100);
	assert(!bq27220_sensor_value(BQ27220_SENSOR_SOC, 101, &value));
	assert(!bq27220_sensor_value(BQ27220_SENSOR_VOLTAGE, 6001, &value));
	assert(!bq27220_sensor_value(BQ27220_SENSOR_TEMPERATURE, 65535, &value));
	assert(!bq27220_sensor_value(BQ27220_NSENSORS, 0, &value));
	assert(!bq27220_sensor_value(BQ27220_SENSOR_SOC, 100, NULL));

	return 0;
}
