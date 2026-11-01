/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/errno.h>
#include <sys/gpio.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include "camera_sensor_power.h"

int
camera_sensor_power_acquire(int phandle, struct camera_sensor_power *power)
{
	int error;

	memset(power, 0, sizeof(*power));
	power->reset = fdtbus_gpio_acquire(phandle, "reset-gpios",
	    GPIO_PIN_OUTPUT);
	if (power->reset == NULL)
		return ENXIO;
	fdtbus_gpio_write(power->reset, 1);

	power->powerdown = fdtbus_gpio_acquire(phandle, "powerdown-gpios",
	    GPIO_PIN_OUTPUT);
	if (power->powerdown == NULL)
		return ENXIO;
	fdtbus_gpio_write(power->powerdown, 1);

	if (!of_hasprop(phandle, "power-supply"))
		return ENOENT;
	power->supply = fdtbus_regulator_acquire(phandle, "power-supply");
	if (power->supply == NULL)
		return ENXIO;

	/* The sensor supply is shared; never disable it on failure or detach. */
	error = fdtbus_regulator_enable(power->supply);
	return error;
}
