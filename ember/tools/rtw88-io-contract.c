/* SPDX-License-Identifier: BSD-2-Clause */
/* The bus boundary is simulated; all register helpers are production code. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_io.h>

struct test_bus {
	uint32_t value;
	unsigned int reads, writes, delays, width;
	int read_error, write_error;
};

static uint32_t
bus_read(struct test_bus *bus, uint32_t address, unsigned int width)
{
	assert(address == 0x40);
	bus->reads++;
	bus->width = width;
	/* Deliberately poison outputs on error, as an untrusted backend may do. */
	return bus->read_error == 0 ? bus->value : 0xdeadbeef;
}

static int
read_1(void *cookie, uint32_t address, uint8_t *value)
{
	struct test_bus *bus = cookie;
	*value = bus_read(bus, address, 1);
	return bus->read_error;
}

static int
read_2(void *cookie, uint32_t address, uint16_t *value)
{
	struct test_bus *bus = cookie;
	*value = bus_read(bus, address, 2);
	return bus->read_error;
}

static int
read_4(void *cookie, uint32_t address, uint32_t *value)
{
	struct test_bus *bus = cookie;
	*value = bus_read(bus, address, 4);
	return bus->read_error;
}

static int
bus_write(struct test_bus *bus, uint32_t address, unsigned int width,
    uint32_t value, uint32_t mask)
{
	assert(address == 0x40);
	bus->writes++;
	bus->width = width;
	/* Even a failed write may have reached the device. */
	bus->value = (bus->value & ~mask) | (value & mask);
	return bus->write_error;
}

static int write_1(void *p, uint32_t a, uint8_t v)
{ return bus_write(p, a, 1, v, 0xff); }
static int write_2(void *p, uint32_t a, uint16_t v)
{ return bus_write(p, a, 2, v, 0xffff); }
static int write_4(void *p, uint32_t a, uint32_t v)
{ return bus_write(p, a, 4, v, 0xffffffff); }
static void delay_us(void *cookie, unsigned int us)
{
	struct test_bus *bus = cookie;
	assert(us == 25);
	bus->delays++;
}

static const struct rtw88_bus_ops test_ops = {
	.read_1 = read_1, .read_2 = read_2, .read_4 = read_4,
	.write_1 = write_1, .write_2 = write_2, .write_4 = write_4,
	.delay_us = delay_us,
};

static void
invalid_width_has_no_io(void)
{
	const unsigned int bad[] = { 0, 3, 5, 8, 0xffffffff };
	for (unsigned int i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
		struct test_bus bus = { .value = 0xa5a53c5a };
		struct rtw88_io io;
		rtw88_io_init(&io, &test_ops, &bus);
		uint32_t value = rtw88_io_rd(&io, 0x40, bad[i]);
		assert(io.error == EINVAL && value == 0);
		assert(bus.reads == 0 && bus.writes == 0);
		rtw88_io_init(&io, &test_ops, &bus);
		rtw88_io_wr(&io, 0x40, bad[i], 0);
		assert(io.error == EINVAL);
		assert(bus.reads == 0 && bus.writes == 0);
		assert(bus.value == 0xa5a53c5a);
	}
}

static void
widths_and_cookie(void)
{
	const unsigned int widths[] = { 1, 2, 4 };
	const uint32_t want[] = { 0x5a, 0x3c5a, 0xa5a53c5a };
	const uint32_t written[] = { 0xa5a53cef, 0xa5a5cdef, 0x89abcdef };
	for (unsigned int i = 0; i < 3; i++) {
		struct test_bus bus = { .value = 0xa5a53c5a };
		struct rtw88_io io;
		rtw88_io_init(&io, &test_ops, &bus);
		uint32_t value = rtw88_io_rd(&io, 0x40, widths[i]);
		assert(value == want[i] && io.error == 0);
		assert(bus.reads == 1 && bus.width == widths[i]);
		rtw88_io_wr(&io, 0x40, widths[i], 0x89abcdef);
		assert(bus.value == written[i] && io.error == 0);
		assert(bus.writes == 1 && bus.width == widths[i]);
		rtw88_io_delay(&io, 25);
		assert(bus.delays == 1);
	}
}

static void
first_error_sticks(void)
{
	for (unsigned int size = 1; size <= 4; size *= 2) {
		struct test_bus bus = { .value = 0xa5a53c5a, .read_error = EIO };
		struct rtw88_io io;
		rtw88_io_init(&io, &test_ops, &bus);
		uint32_t value = rtw88_io_rd(&io, 0x40, size);
		assert(value == 0 && io.error == EIO);
		bus.read_error = EBUSY;
		rtw88_io_set(&io, 0x40, size, 0x01);
		rtw88_io_clr(&io, 0x40, size, 0x80);
		rtw88_io_wr(&io, 0x40, size, 0);
		rtw88_io_delay(&io, 25);
		value = rtw88_io_rd_mask(&io, 0x40, 0);
		rtw88_io_wr_mask(&io, 0x40, 0, 0);
		assert(value == 0 && io.error == EIO);
		assert(bus.reads == 1 && bus.writes == 0 && bus.delays == 0);
	}
	for (unsigned int size = 1; size <= 4; size *= 2) {
		struct test_bus bus = { .write_error = ETIMEDOUT };
		struct rtw88_io io;
		rtw88_io_init(&io, &test_ops, &bus);
		rtw88_io_wr(&io, 0x40, size, 0x12);
		rtw88_io_wr(&io, 0x40, size, 0x34);
		(void)rtw88_io_rd(&io, 0x40, size);
		assert(io.error == ETIMEDOUT && bus.writes == 1 && bus.reads == 0);
		assert(bus.value == 0x12);
	}
}

static void
independent_contexts(void)
{
	struct test_bus a = { .value = 0x12, .read_error = EIO };
	struct test_bus b = { .value = 0x34 };
	struct rtw88_io ia, ib;
	uint32_t value;

	rtw88_io_init(&ia, &test_ops, &a);
	rtw88_io_init(&ib, &test_ops, &b);
	value = rtw88_io_rd(&ia, 0x40, 1);
	assert(value == 0 && ia.error == EIO);
	value = rtw88_io_rd(&ib, 0x40, 1);
	assert(value == 0x34 && ib.error == 0);
	rtw88_io_wr(&ib, 0x40, 1, 0x56);
	rtw88_io_wr(&ia, 0x40, 1, 0x78);
	assert(b.value == 0x56 && b.writes == 1);
	assert(a.value == 0x12 && a.writes == 0);
}

static void
rmw_preserves_unmasked_bits(void)
{
	struct test_bus bus = { .value = 0xa5a53c5a };
	struct rtw88_io io;
	rtw88_io_init(&io, &test_ops, &bus);
	uint32_t value = rtw88_io_rd_mask(&io, 0x40, 0x00000ff0);
	assert(value == 0xc5);
	rtw88_io_wr_mask(&io, 0x40, 0x00ff0000, 0x12);
	assert(bus.value == 0xa5123c5a);
	rtw88_io_set(&io, 0x40, 1, 0x01);
	assert(bus.value == 0xa5123c5b);
	rtw88_io_clr(&io, 0x40, 1, 0x08);
	assert(bus.value == 0xa5123c53);
	assert(io.error == 0);

	memset(&bus, 0, sizeof(bus));
	rtw88_io_init(&io, &test_ops, &bus);
	value = rtw88_io_rd_mask(&io, 0x40, 0);
	assert(value == 0 && io.error == EINVAL && bus.reads == 0);
	rtw88_io_init(&io, &test_ops, &bus);
	rtw88_io_wr_mask(&io, 0x40, 0, 0x12);
	assert(io.error == EINVAL && bus.reads == 0 && bus.writes == 0);
}

static void
missing_callbacks_are_rejected(void)
{
	for (unsigned int i = 0; i < 8; i++) {
		struct rtw88_bus_ops incomplete = test_ops;
		struct test_bus bus = { 0 };
		struct rtw88_io io;
		switch (i) {
		case 0: incomplete.read_1 = NULL; break;
		case 1: incomplete.read_2 = NULL; break;
		case 2: incomplete.read_4 = NULL; break;
		case 3: incomplete.write_1 = NULL; break;
		case 4: incomplete.write_2 = NULL; break;
		case 5: incomplete.write_4 = NULL; break;
		case 6: incomplete.delay_us = NULL; break;
		default: break;
		}
		rtw88_io_init(&io, i == 7 ? NULL : &incomplete, &bus);
		assert(io.error == EINVAL);
		(void)rtw88_io_rd(&io, 0x40, 4);
		rtw88_io_wr(&io, 0x40, 4, 1);
		rtw88_io_delay(&io, 25);
		assert(bus.reads == 0 && bus.writes == 0 && bus.delays == 0);
	}
}

int
main(void)
{
	invalid_width_has_no_io();
	widths_and_cookie();
	first_error_sticks();
	independent_contexts();
	rmw_preserves_unmasked_bits();
	missing_callbacks_are_rejected();
	puts("rtw88 register I/O contract: PASS");
	return 0;
}
