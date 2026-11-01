/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../ov3640_logic.h"

enum event_type {
	EVENT_RESET,
	EVENT_POWERDOWN,
	EVENT_DELAY,
	EVENT_READ,
};

struct event {
	enum event_type type;
	unsigned int value;
};

struct fixture;

struct fake_gpio {
	struct fixture *fixture;
	enum event_type event;
	int active_low;
	int physical_level;
};

struct fdtbus_gpio_pin {
	struct fake_gpio *fake;
};

struct fixture {
	struct ov3640_softc sc;
	struct fake_gpio reset;
	struct fake_gpio powerdown;
	struct fdtbus_gpio_pin reset_pin;
	struct fdtbus_gpio_pin powerdown_pin;
	struct event events[32];
	size_t nevents;
	uint8_t pid_hi;
	uint8_t pid_lo;
	uint16_t fail_reg;
};

static void
record(struct fixture *f, enum event_type type, unsigned int value)
{
	assert(f->nevents < sizeof(f->events) / sizeof(f->events[0]));
	f->events[f->nevents++] = (struct event){type, value};
}

void
fdtbus_gpio_write(struct fdtbus_gpio_pin *pin, int logical_value)
{
	struct fake_gpio *gpio = pin->fake;
	struct fixture *f = gpio->fixture;

	/* Match the ESP FDT provider's logical-to-physical active-low mapping. */
	gpio->physical_level = (logical_value != 0) != (gpio->active_low != 0);
	if (gpio == &f->reset)
		f->sc.test_reset_level = gpio->physical_level;
	else
		f->sc.test_powerdown_level = gpio->physical_level;
	if (gpio->event == EVENT_POWERDOWN && gpio->physical_level == 0)
		assert(f->sc.test_reset_level == 0);
	record(f, gpio->event, gpio->physical_level);
}

static struct fixture *
fixture_from_cookie(const void *cookie)
{
	const struct ov3640_softc *sc = cookie;
	return sc->test_cookie;
}

static void
fixture_init(struct fixture *f, uint8_t pidh, uint8_t pidl)
{
	memset(f, 0, sizeof(*f));
	f->sc.sc_power.reset = &f->reset_pin;
	f->sc.sc_power.powerdown = &f->powerdown_pin;
	f->sc.test_cookie = f;
	f->reset = (struct fake_gpio){f, EVENT_RESET, 1, 1};
	f->powerdown = (struct fake_gpio){f, EVENT_POWERDOWN, 0, 0};
	f->reset_pin.fake = &f->reset;
	f->powerdown_pin.fake = &f->powerdown;
	f->pid_hi = pidh;
	f->pid_lo = pidl;
}

static void
delay_us(void *cookie, unsigned int usec)
{
	record(fixture_from_cookie(cookie), EVENT_DELAY, usec);
}

static int
read_reg(void *cookie, uint16_t reg, uint8_t *value)
{
	struct fixture *f = fixture_from_cookie(cookie);

	/* Sensor communication must happen after physical reset has gone high. */
	assert(f->sc.test_reset_level == 1);
	assert(f->sc.test_powerdown_level == 0);
	record(f, EVENT_READ, reg);
	if (reg == f->fail_reg)
		return EIO;
	if (reg == 0x300a)
		*value = f->pid_hi;
	else if (reg == 0x300b)
		*value = f->pid_lo;
	else
		return EINVAL;
	return 0;
}

static const struct ov3640_sequence_ops ops = {
	.set_reset = ov3640_set_reset,
	.set_powerdown = ov3640_set_powerdown,
	.delay_us = delay_us,
	.read_reg = read_reg,
};

static void
check_success(uint16_t expected)
{
	struct fixture f;
	struct ov3640_sequence_ops test_ops = ops;
	uint16_t pid = 0;

	fixture_init(&f, expected >> 8, expected);
	test_ops.cookie = &f.sc;
	assert(ov3640_identify(&test_ops, &pid) == 0);
	assert(pid == expected);
	assert(f.nevents == 13);
	assert(f.events[0].type == EVENT_RESET && f.events[0].value == 0);
	assert(f.events[1].type == EVENT_POWERDOWN && f.events[1].value == 1);
	assert(f.events[2].type == EVENT_DELAY && f.events[2].value == 20000);
	assert(f.events[3].type == EVENT_DELAY && f.events[3].value == 10000);
	assert(f.events[4].type == EVENT_POWERDOWN && f.events[4].value == 0);
	assert(f.events[5].type == EVENT_DELAY && f.events[5].value == 10000);
	assert(f.events[6].type == EVENT_RESET && f.events[6].value == 1);
	assert(f.events[7].type == EVENT_DELAY && f.events[7].value == 10000);
	assert(f.events[8].type == EVENT_READ && f.events[8].value == 0x300a);
	assert(f.events[9].type == EVENT_READ && f.events[9].value == 0x300b);
	assert(f.events[10].type == EVENT_RESET && f.events[10].value == 0);
	assert(f.events[11].type == EVENT_POWERDOWN && f.events[11].value == 1);
	assert(f.events[12].type == EVENT_DELAY && f.events[12].value == 10000);
}

static void
check_parked(const struct fixture *f)
{
	assert(f->nevents >= 3);
	assert(f->events[f->nevents - 3].type == EVENT_RESET &&
	    f->events[f->nevents - 3].value == 0);
	assert(f->events[f->nevents - 2].type == EVENT_POWERDOWN &&
	    f->events[f->nevents - 2].value == 1);
	assert(f->events[f->nevents - 1].type == EVENT_DELAY &&
	    f->events[f->nevents - 1].value == 10000);
}

int
main(void)
{
	struct fixture f;
	struct ov3640_sequence_ops test_ops = ops;
	uint16_t pid;

	check_success(0x364c);
	check_success(0x3641);

	fixture_init(&f, 0x36, 0x4c);
	test_ops.cookie = &f.sc;
	f.fail_reg = 0x300a;
	pid = 0xffff;
	assert(ov3640_identify(&test_ops, &pid) == EIO);
	assert(pid == 0);
	check_parked(&f);

	fixture_init(&f, 0x12, 0x34);
	test_ops.cookie = &f.sc;
	pid = 0xffff;
	assert(ov3640_identify(&test_ops, &pid) == ENODEV);
	assert(pid == 0x1234);
	check_parked(&f);
	return 0;
}
