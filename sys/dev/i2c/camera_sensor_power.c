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
	/* Optional flash output is inactive before any sensor control or supply. */
	if (of_hasprop(phandle, "flash-gpios")) {
		power->flash = fdtbus_gpio_acquire(phandle, "flash-gpios",
		    GPIO_PIN_OUTPUT);
		if (power->flash == NULL)
			return ENXIO;
		fdtbus_gpio_write(power->flash, 0);
	}
	power->reset = fdtbus_gpio_acquire(phandle, "reset-gpios",
	    GPIO_PIN_OUTPUT);
	if (power->reset == NULL) {
		error = ENXIO;
		goto fail;
	}
	fdtbus_gpio_write(power->reset, 1);

	power->powerdown = fdtbus_gpio_acquire(phandle, "powerdown-gpios",
	    GPIO_PIN_OUTPUT);
	if (power->powerdown == NULL) {
		error = ENXIO;
		goto fail;
	}
	fdtbus_gpio_write(power->powerdown, 1);

	if (!of_hasprop(phandle, "power-supply")) {
		error = ENOENT;
		goto fail;
	}
	power->supply = fdtbus_regulator_acquire(phandle, "power-supply");
	if (power->supply == NULL) {
		error = ENXIO;
		goto fail;
	}

	/* The sensor supply is shared; never disable it on failure or detach. */
	error = fdtbus_regulator_enable(power->supply);
	if (error == 0)
		return 0;

fail:
	camera_sensor_power_release_flash(power);
	return error;
}

void
camera_sensor_power_release_flash(struct camera_sensor_power *power)
{
	if (power->flash == NULL)
		return;
	fdtbus_gpio_write(power->flash, 0);
	fdtbus_gpio_release(power->flash);
	power->flash = NULL;
}
