/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "../camera_sensor_power.h"

#define GPIO_PIN_OUTPUT 1

struct fdtbus_gpio_pin {
	int logical;
	int physical;
	int active_low;
};

struct fdtbus_regulator {
	int enabled;
};

struct fixture {
	struct fdtbus_gpio_pin reset;
	struct fdtbus_gpio_pin powerdown;
	struct fdtbus_regulator supply;
	int fail_reset;
	int fail_powerdown;
	int have_supply;
	int fail_supply_acquire;
	int supply_error;
	unsigned int acquire_count;
	unsigned int enable_count;
};

static struct fixture current;

struct fdtbus_gpio_pin *fdtbus_gpio_acquire(int, const char *, int);
void fdtbus_gpio_write(struct fdtbus_gpio_pin *, int);
bool of_hasprop(int, const char *);
struct fdtbus_regulator *fdtbus_regulator_acquire(int, const char *);
int fdtbus_regulator_enable(struct fdtbus_regulator *);

struct fdtbus_gpio_pin *
fdtbus_gpio_acquire(int phandle, const char *name, int flags)
{
	(void)phandle;
	assert(flags == GPIO_PIN_OUTPUT);
	current.acquire_count++;
	if (strcmp(name, "reset-gpios") == 0)
		return current.fail_reset ? NULL : &current.reset;
	assert(strcmp(name, "powerdown-gpios") == 0);
	return current.fail_powerdown ? NULL : &current.powerdown;
}

void
fdtbus_gpio_write(struct fdtbus_gpio_pin *pin, int logical)
{
	pin->logical = !!logical;
	pin->physical = (pin->logical != 0) != (pin->active_low != 0);
}

bool
of_hasprop(int phandle, const char *name)
{
	(void)phandle;
	assert(strcmp(name, "power-supply") == 0);
	return current.have_supply;
}

struct fdtbus_regulator *
fdtbus_regulator_acquire(int phandle, const char *name)
{
	(void)phandle;
	assert(strcmp(name, "power-supply") == 0);
	return current.fail_supply_acquire ? NULL : &current.supply;
}

int
fdtbus_regulator_enable(struct fdtbus_regulator *supply)
{
	assert(supply == &current.supply);
	current.enable_count++;
	if (current.supply_error != 0)
		return current.supply_error;
	supply->enabled = 1;
	return 0;
}

static void
fixture_init(void)
{
	memset(&current, 0, sizeof(current));
	current.reset.active_low = 1;
	current.powerdown.active_low = 0;
	current.have_supply = 1;
}

int
main(void)
{
	struct camera_sensor_power power;
	int error;

	fixture_init();
	error = camera_sensor_power_acquire(1, &power);
	assert(error == 0);
	assert(power.reset == &current.reset);
	assert(power.powerdown == &current.powerdown);
	assert(power.supply == &current.supply && current.supply.enabled);
	assert(current.reset.logical == 1 && current.reset.physical == 0);
	assert(current.powerdown.logical == 1 && current.powerdown.physical == 1);
	assert(current.acquire_count == 2 && current.enable_count == 1);

	fixture_init();
	current.fail_reset = 1;
	error = camera_sensor_power_acquire(1, &power);
	assert(error == ENXIO);
	assert(current.acquire_count == 1 && current.enable_count == 0);

	fixture_init();
	current.fail_powerdown = 1;
	error = camera_sensor_power_acquire(1, &power);
	assert(error == ENXIO);
	assert(current.reset.physical == 0 && current.enable_count == 0);

	fixture_init();
	current.have_supply = 0;
	error = camera_sensor_power_acquire(1, &power);
	assert(error == ENOENT);
	assert(current.reset.physical == 0 && current.powerdown.physical == 1);
	assert(current.enable_count == 0);

	fixture_init();
	current.fail_supply_acquire = 1;
	error = camera_sensor_power_acquire(1, &power);
	assert(error == ENXIO);
	assert(current.reset.physical == 0 && current.powerdown.physical == 1);
	assert(current.enable_count == 0);

	fixture_init();
	current.supply_error = EIO;
	error = camera_sensor_power_acquire(1, &power);
	assert(error == EIO);
	assert(current.reset.physical == 0 && current.powerdown.physical == 1);
	assert(current.enable_count == 1 && !current.supply.enabled);
	return 0;
}
