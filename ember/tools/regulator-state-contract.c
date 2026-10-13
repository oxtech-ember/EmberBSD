/* Origin: EmberBSD; production FDT and AXP8191 regulator state contracts. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef unsigned int u_int;
typedef void *device_t;
typedef void *i2c_tag_t;
typedef unsigned int i2c_addr_t;
typedef int callout_t;
struct sysmon_pswitch { int unused; };
#define NBBY 8
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define howmany(x, y) (((x) + (y) - 1) / (y))
#define device_private(d) (d)

#include "fdt.h"
#include "axp.h"
struct fdtbus_regulator_controller;
struct fdtbus_regulator { struct fdtbus_regulator_controller *reg_rc; };
struct fdtbus_regulator_controller {
	device_t rc_dev;
	int rc_phandle;
	const struct fdtbus_regulator_controller_func *rc_funcs;
	u_int rc_enable_ramp_delay;
};
#define REGULATOR_TO_RC(r) ((r)->reg_rc)

static uint8_t registers[256], last_register;
static unsigned acquisitions, reads, writes, releases, delays, checks;
static int acquire_error, read_error, query_error;
static unsigned query_calls, enable_calls;
static bool bus_owned, always_on, query_state, last_enable;

static void
check(bool condition, const char *message)
{
	checks++;
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		assert(condition);
	}
}

static int
iic_acquire_bus(i2c_tag_t tag, int flags)
{
	acquisitions++;
	assert(!bus_owned);
	if (acquire_error == 0)
		bus_owned = true;
	return acquire_error;
}

static int
iic_smbus_read_byte(i2c_tag_t tag, i2c_addr_t addr, uint8_t reg,
    uint8_t *value, int flags)
{
	assert(bus_owned);
	reads++;
	last_register = reg;
	/* Even a failed transport may have overwritten its own result. */
	*value = registers[reg];
	return read_error;
}

static int
iic_smbus_write_byte(i2c_tag_t tag, i2c_addr_t addr, uint8_t reg,
    uint8_t value, int flags)
{
	assert(bus_owned);
	writes++;
	registers[reg] = value;
	return 0;
}

static void
iic_release_bus(i2c_tag_t tag, int flags)
{
	assert(bus_owned);
	bus_owned = false;
	releases++;
}

static void
delay(u_int us)
{
	delays++;
}

static bool
of_hasprop(int phandle, const char *name)
{
	assert(strcmp(name, "regulator-always-on") == 0);
	return always_on;
}

#include "fdt-functions.h"
#include "axp-functions.h"

static int
legacy_enable(device_t dev, bool enable)
{
	enable_calls++;
	last_enable = enable;
	return query_error;
}

static int
checked_query(device_t dev, bool *value)
{
	query_calls++;
	*value = query_state;
	return query_error;
}

static const struct fdtbus_regulator_controller_func legacy_funcs = {
	.enable = legacy_enable,
};
static const struct fdtbus_regulator_controller_func checked_funcs = {
	.is_enabled = checked_query,
};

static void
clear_io(void)
{
	acquisitions = reads = writes = releases = delays = 0;
	acquire_error = read_error = 0;
	assert(!bus_owned);
}

static void
test_interface(void)
{
	struct fdtbus_regulator_controller rc = { .rc_funcs = &legacy_funcs };
	struct fdtbus_regulator reg = { .reg_rc = &rc };
	const int errors[] = { ENXIO, EIO, ETIMEDOUT, EOPNOTSUPP };
	bool enabled;
	u_int i;

	for (i = 0; i < 2; i++) {
		enabled = i != 0;
		check(fdtbus_regulator_is_enabled(&reg, &enabled) == EOPNOTSUPP,
		    "legacy provider reports unsupported state");
		check(enabled == (i != 0), "unsupported preserves result");
	}
	check(enable_calls == 0, "state never invokes legacy enable");
	rc.rc_enable_ramp_delay = 1;
	check(fdtbus_regulator_enable(&reg) == 0 && last_enable,
	    "legacy enable remains callable");
	check(enable_calls == 1 && delays == 1, "legacy ramp delay remains");
	check(fdtbus_regulator_disable(&reg) == 0 && !last_enable,
	    "legacy disable remains callable");
	always_on = true;
	check(fdtbus_regulator_disable(&reg) == EIO && enable_calls == 2,
	    "legacy always-on disable is still rejected");
	always_on = false;
	rc.rc_funcs = &checked_funcs;
	check(fdtbus_regulator_is_enabled(&reg, NULL) == EINVAL &&
	    query_calls == 0, "null result rejected before callback");
	for (i = 0; i < __arraycount(errors); i++) {
		query_error = errors[i];
		for (unsigned value = 0; value < 2; value++) {
			enabled = value != 0;
			query_state = !enabled;
			check(fdtbus_regulator_is_enabled(&reg, &enabled) ==
			    errors[i], "checked error propagates");
			check(enabled == (value != 0),
			    "wrapper contains failed callback output");
		}
	}
	query_error = 0;
	for (i = 0; i < 2; i++) {
		query_state = i != 0;
		enabled = !query_state;
		check(fdtbus_regulator_is_enabled(&reg, &enabled) == 0 &&
		    enabled == query_state, "successful query publishes state");
	}
}

static void
test_axp8191(void)
{
	struct axp8191_softc pmic = { 0 };
	struct axp8191reg_softc sc = { .sc_pmic = &pmic };
	struct fdtbus_regulator_controller rc = {
		.rc_dev = &sc, .rc_funcs = &axp8191reg_funcs,
	};
	struct fdtbus_regulator reg = { .reg_rc = &rc };
	const int errors[] = { EBUSY, ENXIO, EIO, ETIMEDOUT };
	bool enabled;
	u_int i, j;

	check(__arraycount(axp8191_ctrls) == 39, "all 39 regulator entries");
	check(axp8191_ctrls[1].c_enable_reg == 0x10 &&
	    axp8191_ctrls[1].c_enable_mask == __BIT(1), "NPU DCDC2 mapping");
	check(axp8191_ctrls[3].c_enable_reg == 0x10 &&
	    axp8191_ctrls[3].c_enable_mask == __BIT(3), "GPU DCDC4 mapping");
	for (i = 0; i < __arraycount(axp8191_ctrls); i++) {
		const struct axp8191_ctrl *c = &axp8191_ctrls[i];

		sc.sc_ctrl = c;
		/* Exercise every byte, including unrelated enable bits. */
		for (j = 0; j <= UINT8_MAX; j++) {
			clear_io();
			memset(registers, 0xa5, sizeof(registers));
			registers[c->c_enable_reg] = j;
			enabled = (j & c->c_enable_mask) == 0;
			check(fdtbus_regulator_is_enabled(&reg, &enabled) == 0,
			    "AXP query succeeds");
			check(enabled == ((j & c->c_enable_mask) != 0),
			    "AXP query isolates the selected enable bit");
			check(acquisitions == 1 && reads == 1 && releases == 1 &&
			    last_register == c->c_enable_reg,
			    "one serialized read of the selected register");
			check(writes == 0 && delays == 0, "query has no mutation");
		}
	}
	for (i = 0; i < __arraycount(errors); i++) {
		for (j = 0; j < 2; j++) {
			clear_io();
			acquire_error = errors[i];
			enabled = j != 0;
			check(fdtbus_regulator_is_enabled(&reg, &enabled) ==
			    errors[i] && enabled == (j != 0),
			    "I2C acquire failure preserves result");
			check(acquisitions == 1 && reads == 0 && releases == 0 &&
			    writes == 0, "acquire failure performs no transfer");
			clear_io();
			read_error = errors[i];
			registers[sc.sc_ctrl->c_enable_reg] = j ? 0 : UINT8_MAX;
			check(axp8191reg_is_enabled(&sc, &enabled) == errors[i] &&
			    enabled == (j != 0), "AXP callback preserves error result");
			check(acquisitions == 1 && reads == 1 && releases == 1 &&
			    writes == 0, "read failure releases bus without writes");
			clear_io();
			check(fdtbus_regulator_is_enabled(&reg, &enabled) == 0 &&
			    enabled != (j != 0), "fresh query works after failure");
		}
	}
}

int
main(void)
{
	test_interface();
	test_axp8191();
	printf("PASS: %u regulator state checks\n", checks);
	return 0;
}
