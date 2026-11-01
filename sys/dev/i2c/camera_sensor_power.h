/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _DEV_I2C_CAMERA_SENSOR_POWER_H_
#define _DEV_I2C_CAMERA_SENSOR_POWER_H_

struct fdtbus_gpio_pin;
struct fdtbus_regulator;

struct camera_sensor_power {
	struct fdtbus_gpio_pin *flash;
	struct fdtbus_gpio_pin *reset;
	struct fdtbus_gpio_pin *powerdown;
	struct fdtbus_regulator *supply;
};

int camera_sensor_power_acquire(int, struct camera_sensor_power *);
void camera_sensor_power_release_flash(struct camera_sensor_power *);

#endif /* _DEV_I2C_CAMERA_SENSOR_POWER_H_ */
