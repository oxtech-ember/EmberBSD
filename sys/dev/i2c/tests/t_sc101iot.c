/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../sc101iot_logic.h"

typedef void *i2c_tag_t;
typedef uint16_t i2c_addr_t;
typedef unsigned int i2c_op_t;

#define I2C_OP_WRITE_WITH_STOP 1
#define I2C_OP_READ_WITH_STOP 2

struct fixture;

struct fake_gpio {
	struct fixture *fixture;
	int active_low;
	int physical_level;
	unsigned int event;
};

struct fdtbus_gpio_pin {
	struct fake_gpio *fake;
};

struct camera_sensor_power {
	struct fdtbus_gpio_pin *reset;
	struct fdtbus_gpio_pin *powerdown;
};

struct sc101iot_softc {
	i2c_tag_t sc_tag;
	i2c_addr_t sc_addr;
	struct camera_sensor_power sc_power;
	struct fixture *test_fixture;
};

enum event_type {
	EVENT_RESET,
	EVENT_POWERDOWN,
	EVENT_DELAY,
	EVENT_PAGE_WRITE,
	EVENT_REG_READ,
};

struct event {
	enum event_type type;
	unsigned int value;
};

struct fixture {
	struct sc101iot_softc sc;
	struct fake_gpio reset;
	struct fake_gpio powerdown;
	struct fdtbus_gpio_pin reset_pin;
	struct fdtbus_gpio_pin powerdown_pin;
	struct event events[48];
	size_t nevents;
	uint8_t selected_page;
	uint8_t pid_hi;
	uint8_t pid_lo;
	int fail_page;
	int fail_reg;
	int fail_acquire;
	int bus_acquired;
	unsigned int acquires;
	unsigned int releases;
};

static int sc101iot_set_reset(void *, int);
static int sc101iot_set_powerdown(void *, int);
static int sc101iot_read_reg(struct sc101iot_softc *, uint16_t, uint8_t *);
static int sc101iot_read_reg_cb(void *, uint16_t, uint8_t *);

static void record(struct fixture *, enum event_type, unsigned int);
static void fdtbus_gpio_write(struct fdtbus_gpio_pin *, int);
static int iic_acquire_bus(i2c_tag_t, int);
static void iic_release_bus(i2c_tag_t, int);
static int iic_exec(i2c_tag_t, i2c_op_t, i2c_addr_t, const void *, size_t,
    void *, size_t, int);

static void
record(struct fixture *f, enum event_type type, unsigned int value)
{
	assert(f->nevents < sizeof(f->events) / sizeof(f->events[0]));
	f->events[f->nevents++] = (struct event){type, value};
}

static void
fdtbus_gpio_write(struct fdtbus_gpio_pin *pin, int logical_value)
{
	struct fake_gpio *gpio = pin->fake;
	struct fixture *f = gpio->fixture;

	/* Model FDT ACTIVE_LOW translation; driver callbacks pass logical state. */
	gpio->physical_level = (logical_value != 0) != (gpio->active_low != 0);
	if (gpio == &f->reset && gpio->physical_level == 1)
		assert(f->sc.test_fixture != NULL);
	if (gpio == &f->powerdown && gpio->physical_level == 0)
		assert(f->reset.physical_level == 0);
	record(f, gpio->event, gpio->physical_level);
}

static int
iic_acquire_bus(i2c_tag_t tag, int flags)
{
	struct fixture *f = tag;

	(void)flags;
	assert(!f->bus_acquired);
	if (f->fail_acquire)
		return EIO;
	f->bus_acquired = 1;
	f->acquires++;
	return 0;
}

static void
iic_release_bus(i2c_tag_t tag, int flags)
{
	struct fixture *f = tag;

	(void)flags;
	assert(f->bus_acquired);
	f->bus_acquired = 0;
	f->releases++;
}

static int
iic_exec(i2c_tag_t tag, i2c_op_t op, i2c_addr_t addr, const void *cmd,
    size_t cmdlen, void *data, size_t datalen, int flags)
{
	struct fixture *f = tag;
	uint8_t bytes[2] = { 0 };

	(void)flags;
	assert(f->bus_acquired);
	assert(addr == 0x68);
	assert(cmdlen <= sizeof(bytes));
	memcpy(bytes, cmd, cmdlen);
	if (op == I2C_OP_WRITE_WITH_STOP) {
		assert(cmdlen == 2 && datalen == 0 && data == NULL);
		assert(bytes[0] == 0xf0);
		record(f, EVENT_PAGE_WRITE, bytes[1]);
		if (f->fail_page)
			return EIO;
		f->selected_page = bytes[1];
		return 0;
	}
	assert(op == I2C_OP_READ_WITH_STOP);
	assert(cmdlen == 1 && datalen == 1 && data != NULL);
	assert(f->selected_page == 0x31);
	assert(f->reset.physical_level == 1);
	assert(f->powerdown.physical_level == 0);
	record(f, EVENT_REG_READ, bytes[0]);
	if (f->fail_reg == bytes[0])
		return EIO;
	if (bytes[0] == 0xf7)
		*(uint8_t *)data = f->pid_hi;
	else if (bytes[0] == 0xf8)
		*(uint8_t *)data = f->pid_lo;
	else
		return EINVAL;
	return 0;
}

static void
fixture_init(struct fixture *f, uint8_t pidh, uint8_t pidl)
{
	memset(f, 0, sizeof(*f));
	f->sc.sc_tag = f;
	f->sc.sc_addr = 0x68;
	f->sc.sc_power.reset = &f->reset_pin;
	f->sc.sc_power.powerdown = &f->powerdown_pin;
	f->sc.test_fixture = f;
	f->reset = (struct fake_gpio){f, 1, 0, EVENT_RESET};
	f->powerdown = (struct fake_gpio){f, 0, 1, EVENT_POWERDOWN};
	f->reset_pin.fake = &f->reset;
	f->powerdown_pin.fake = &f->powerdown;
	f->pid_hi = pidh;
	f->pid_lo = pidl;
}

static void
delay_us(void *cookie, unsigned int usec)
{
	struct sc101iot_softc *sc = cookie;

	record(sc->test_fixture, EVENT_DELAY, usec);
}

static const struct sc101iot_sequence_ops ops = {
	.set_reset = sc101iot_set_reset,
	.set_powerdown = sc101iot_set_powerdown,
	.delay_us = delay_us,
	.read_reg = sc101iot_read_reg_cb,
};

static void
check_success(void)
{
	struct fixture f;
	struct sc101iot_sequence_ops test_ops = ops;
	uint16_t pid = 0;

	fixture_init(&f, 0xda, 0x4a);
	test_ops.cookie = &f.sc;
	assert(sc101iot_identify(&test_ops, &pid) == 0);
	assert(pid == 0xda4a);
	assert(f.acquires == 2 && f.releases == 2 && !f.bus_acquired);
	assert(f.nevents == 17);
	assert(f.events[0].type == EVENT_RESET && f.events[0].value == 0);
	assert(f.events[1].type == EVENT_POWERDOWN && f.events[1].value == 1);
	assert(f.events[2].type == EVENT_DELAY && f.events[2].value == 20000);
	assert(f.events[3].type == EVENT_DELAY && f.events[3].value == 10000);
	assert(f.events[4].type == EVENT_POWERDOWN && f.events[4].value == 0);
	assert(f.events[5].type == EVENT_DELAY && f.events[5].value == 10000);
	assert(f.events[6].type == EVENT_RESET && f.events[6].value == 0);
	assert(f.events[7].type == EVENT_DELAY && f.events[7].value == 10000);
	assert(f.events[8].type == EVENT_RESET && f.events[8].value == 1);
	assert(f.events[9].type == EVENT_DELAY && f.events[9].value == 10000);
	assert(f.events[10].type == EVENT_PAGE_WRITE && f.events[10].value == 0x31);
	assert(f.events[11].type == EVENT_REG_READ && f.events[11].value == 0xf7);
	assert(f.events[12].type == EVENT_PAGE_WRITE && f.events[12].value == 0x31);
	assert(f.events[13].type == EVENT_REG_READ && f.events[13].value == 0xf8);
	assert(f.events[14].type == EVENT_RESET && f.events[14].value == 0);
	assert(f.events[15].type == EVENT_POWERDOWN && f.events[15].value == 1);
	assert(f.events[16].type == EVENT_DELAY && f.events[16].value == 10000);
}

int
main(void)
{
	struct fixture f;
	struct sc101iot_sequence_ops test_ops = ops;
	uint16_t pid;

	check_success();
	fixture_init(&f, 0xda, 0x4a);
	test_ops.cookie = &f.sc;
	f.fail_page = 1;
	assert(sc101iot_identify(&test_ops, &pid) == EIO);
	assert(f.acquires == 1 && f.releases == 1 && !f.bus_acquired);
	assert(f.reset.physical_level == 0 && f.powerdown.physical_level == 1);

	fixture_init(&f, 0xda, 0x4a);
	test_ops.cookie = &f.sc;
	f.fail_reg = 0xf7;
	assert(sc101iot_identify(&test_ops, &pid) == EIO);
	assert(f.acquires == 1 && f.releases == 1 && !f.bus_acquired);
	assert(f.reset.physical_level == 0 && f.powerdown.physical_level == 1);

	fixture_init(&f, 0xda, 0x4a);
	test_ops.cookie = &f.sc;
	f.fail_reg = 0xf8;
	assert(sc101iot_identify(&test_ops, &pid) == EIO);
	assert(f.acquires == 2 && f.releases == 2 && !f.bus_acquired);
	assert(f.reset.physical_level == 0 && f.powerdown.physical_level == 1);

	fixture_init(&f, 0xda, 0x4a);
	test_ops.cookie = &f.sc;
	f.fail_acquire = 1;
	assert(sc101iot_identify(&test_ops, &pid) == EIO);
	assert(f.acquires == 0 && f.releases == 0 && !f.bus_acquired);
	assert(f.reset.physical_level == 0 && f.powerdown.physical_level == 1);

	fixture_init(&f, 0x12, 0x34);
	test_ops.cookie = &f.sc;
	pid = 0;
	assert(sc101iot_identify(&test_ops, &pid) == ENODEV);
	assert(pid == 0x1234);
	assert(f.reset.physical_level == 0 && f.powerdown.physical_level == 1);
	return 0;
}
