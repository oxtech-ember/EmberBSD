/* SPDX-License-Identifier: BSD-2-Clause */
/* Model only device behavior; execute the production parser and sequences. */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw8723d_boot.h>

#define LOCAL 0x10250000U

struct model {
	uint8_t mac[65536], local[4096], image[32768];
	size_t expected, written;
	unsigned events, writes, windows, waits, delay_calls, delay_us;
	unsigned fail_event, cancel_event;
	bool cancel_enabled, stuck_power, stuck_enable, stuck_report, stuck_ready;
	bool fail_window, fail_cleanup;
	bool fault_delivered;
	unsigned cleanup_attempts;
	unsigned unmasked_shutdowns;
	unsigned shutdowns;
	unsigned regulator_event, power_event;
};

static uint32_t
load32(const uint8_t *p)
{

	return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	    (uint32_t)p[3] << 24;
}

static void
store32(uint8_t *p, uint32_t value)
{
	unsigned i;

	for (i = 0; i < 4; i++)
		p[i] = (uint8_t)(value >> (8 * i));
}

static uint8_t *
regptr(struct model *m, uint32_t address, unsigned width)
{

	if ((address & 0xffff0000U) == LOCAL) {
		address &= 0xffff;
		assert(address <= sizeof(m->local) - width);
		return m->local + address;
	}
	assert(address <= sizeof(m->mac) - width);
	return m->mac + address;
}

static int
read_reg(struct model *m, uint32_t address, unsigned width, uint32_t *value)
{
	uint8_t *p = regptr(m, address, width);
	unsigned i;

	m->events++;
	if (m->fail_cleanup && m->windows != 0) {
		m->cleanup_attempts++;
		return EIO;
	}
	if (m->fail_event != 0 && m->events == m->fail_event) {
		m->fault_delivered = true;
		*value = 0xdeadbeef;
		return EIO;
	}
	*value = 0;
	for (i = 0; i < width; i++)
		*value |= (uint32_t)p[i] << (8 * i);
	if (address == 0x06 && m->stuck_power)
		*value &= ~2U;
	return 0;
}

static int
write_reg(struct model *m, uint32_t address, unsigned width, uint32_t value)
{
	uint8_t *p = regptr(m, address, width);
	unsigned i;
	uint8_t old_control = m->mac[0x80];

	m->events++;
	m->writes++;
	if (address == 0x007c)
		m->regulator_event = m->events;
	if (address == 0x0005 && m->power_event == 0)
		m->power_event = m->events;
	if (address == 0x0522) {
		m->shutdowns++;
		if (load32(m->local + 0x14) != 0)
			m->unmasked_shutdowns++;
	}
	/* Apply even a failed write: completion may be uncertain. */
	for (i = 0; i < width; i++)
		p[i] = (uint8_t)(value >> (8 * i));
	if (address == 0x80) {
		/* Report is W1C; other low-byte fields are normal R/W. */
		m->mac[0x80] = (m->mac[0x80] & ~4U) |
		    ((value & 4) == 0 ? old_control & 4 : 0);
		if (m->stuck_enable)
			m->mac[0x80] &= ~1U;
	}
	if (address == 0x05) {
		m->mac[0x05] &= ~3U; /* Hardware completes on/off requests. */
		if (value & 1)
			m->mac[0x100] = 0;
	}
	if (address == LOCAL + 0x86) {
		if (value & 1) {
			m->local[0x86] &= ~2U;
			m->mac[0x100] = 0xea; /* Powered-off CR sentinel. */
			m->mac[0x101] = 0;
		} else {
			m->local[0x86] |= 2;
		}
	}
	if (address == 0x03 && (value & 4) != 0 &&
	    (m->mac[0x80] & 2) != 0 && m->written == m->expected &&
	    !m->stuck_ready)
		m->mac[0x80] |= 0xc6;
	if (m->fail_event != 0 && m->events == m->fail_event) {
		m->fault_delivered = true;
		return EIO;
	}
	return 0;
}

static int read_1(void *p, uint32_t a, uint8_t *v)
{ uint32_t n = 0; int e = read_reg(p, a, 1, &n); *v = n; return e; }
static int read_2(void *p, uint32_t a, uint16_t *v)
{ uint32_t n = 0; int e = read_reg(p, a, 2, &n); *v = n; return e; }
static int read_4(void *p, uint32_t a, uint32_t *v)
{ return read_reg(p, a, 4, v); }
static int write_1(void *p, uint32_t a, uint8_t v)
{ return write_reg(p, a, 1, v); }
static int write_2(void *p, uint32_t a, uint16_t v)
{ return write_reg(p, a, 2, v); }
static int write_4(void *p, uint32_t a, uint32_t v)
{ return write_reg(p, a, 4, v); }

static void
delay_us(void *cookie, unsigned us)
{
	struct model *m = cookie;

	m->events++;
	m->delay_calls++;
	m->delay_us += us;
}

static const struct rtw88_bus_ops test_bus_ops = {
	.read_1 = read_1, .read_2 = read_2, .read_4 = read_4,
	.write_1 = write_1, .write_2 = write_2, .write_4 = write_4,
	.delay_us = delay_us
};

static int
write_window(void *cookie, uint32_t address, const uint8_t *data, size_t length)
{
	struct model *m = cookie;
	unsigned page = (load32(m->mac + 0x80) >> 16) & 7;

	m->events++;
	m->windows++;
	assert(address >= 0x1000 && address <= 0x1ffc);
	assert(length == 4 && (address & 3) == 0);
	memcpy(m->image + page * 4096 + address - 0x1000, data, length);
	m->written += length;
	if (m->written == m->expected && !m->stuck_report)
		m->mac[0x80] |= 4;
	if (m->fail_window)
		return ENOSPC;
	if (m->fail_event != 0 && m->events == m->fail_event) {
		m->fault_delivered = true;
		return EIO;
	}
	return 0;
}

static void
wait_ms(void *cookie, unsigned ms)
{
	struct model *m = cookie;

	m->events++;
	m->waits += ms;
}

static bool
cancelled(void *cookie)
{
	struct model *m = cookie;

	return m->cancel_enabled && m->events >= m->cancel_event;
}

static void
setup(struct model *m, struct rtw88_io *io, struct rtw8723d_boot_ops *ops)
{

	memset(m, 0, sizeof(*m));
	m->mac[0x05] = 0xc0;
	m->mac[0x06] = 2;
	m->mac[0x1c] = 0xa5;
	m->mac[0x08] = 0xa1;
	m->mac[0x100] = 0xea;
	store32(m->mac + 0x40, 0xa5a5b5a5);
	m->local[0x86] = 2;
	store32(m->local + 0x14, 0x5aa5a55a);
	store32(m->mac + 0x80, 0x80000);
	rtw88_io_init(io, &test_bus_ops, m);
	*ops = (struct rtw8723d_boot_ops) {
		.write_window = write_window, .wait_ms = wait_ms,
		.cancelled = cancelled, .cookie = m
	};
}

static void
parser_contract(void)
{
	uint8_t blob[32801] = { 0xd1, 0x23, 0x10, 0, 0x34, 0x12 };
	struct rtw8723d_fw fw, before;
	unsigned which;
	size_t length;

	blob[12] = 3;
	blob[32] = 1; blob[33] = 2; blob[34] = 3;
	assert(rtw8723d_fw_parse(blob, 35, &fw) == 0);
	assert(fw.payload == blob + 32 && fw.payload_size == 3);
	assert(fw.version == 0x1234); /* Version is not pinned to 48. */
	before = fw;
	for (which = 0; which < 6; which++) {
		length = 35;
		blob[0] = 0xd1; blob[12] = 3; blob[13] = 0;
		if (which == 0) length = 31;
		if (which == 1) blob[0] = 0xd2;
		if (which == 2) length = 34;
		if (which == 3) { blob[12] = 0; length = 32; }
		if (which == 4) { blob[12] = 1; blob[13] = 0x80; length = 32801; }
		if (which == 5) length = 0;
		assert(rtw8723d_fw_parse(blob, length, &fw) != 0);
		assert(fw.payload == before.payload && fw.payload_size == before.payload_size);
		assert(fw.version == before.version);
	}
	assert(rtw8723d_fw_parse(NULL, 35, &fw) == EINVAL);
	assert(rtw8723d_fw_parse(blob, 35, NULL) == EINVAL);
	blob[12] = 0; blob[13] = 0x80;
	assert(rtw8723d_fw_parse(blob, 32800, &fw) == 0);
	assert(fw.payload_size == 32768);
	puts("firmware parser bounds and unchanged outputs: PASS");
}

static void
identity_and_power(void)
{
	struct model m;
	struct rtw88_io io;
	struct rtw8723d_boot_ops ops;
	struct rtw8723d_identity id;
	unsigned events, at;
	int error;

	setup(&m, &io, &ops);
	store32(m.mac + 0xf0, 0x08002000);
	assert(rtw8723d_identify(&io, &id) == 0);
	assert(id.sys_cfg1 == 0x08002000 && id.cut == 2 && id.rf_paths == 2);
	setup(&m, &io, &ops);
	m.fail_event = 1;
	assert(rtw8723d_identify(&io, &id) == EIO);
	assert(id.sys_cfg1 == 0x08002000 && id.cut == 2 && id.rf_paths == 2);
	setup(&m, &io, &ops);
	assert(rtw8723d_power_on(&io, &ops) == 0);
	assert(load32(m.local + 0x14) == 0x5aa5a55a);
	assert(m.mac[0x1c] == 0 && m.mac[0x7c] == 0x83);
	assert(m.regulator_event != 0 && m.regulator_event < m.power_event);
	assert(m.mac[0x100] == 0xff && m.mac[0x101] == 2);
	assert(m.mac[0x423] == 0x7f && m.mac[0x08] == 0xa9);
	assert(load32(m.mac + 0x40) == 0xa5a5a5a5);
	assert(m.mac[0x1f] == 7 && m.mac[0x77] == 7);
	assert((m.mac[0x05] & 0x40) != 0 && m.waits == 5);
	events = m.events;
	setup(&m, &io, &ops);
	store32(m.mac + 0xf0, 0x01000000);
	assert(rtw8723d_power_on(&io, &ops) == 0);
	assert(m.mac[0x7c] == 0xc3);
	/* Recover an already-active MAC once, as the donor wrapper does. */
	setup(&m, &io, &ops);
	m.mac[0x100] = 0xff; m.mac[0x101] = 2;
	assert(rtw8723d_power_on(&io, &ops) == 0);
	assert(m.shutdowns == 1 && m.unmasked_shutdowns == 0);
	assert(m.mac[0x100] == 0xff && m.mac[0x101] == 2);
	/* Cancellation can arrive during the last interrupt-restore write. */
	setup(&m, &io, &ops);
	m.cancel_enabled = true;
	m.cancel_event = events;
	assert(rtw8723d_power_on(&io, &ops) == ECANCELED);
	assert(m.unmasked_shutdowns == 0);
	assert(load32(m.local + 0x14) == 0x5aa5a55a);
	for (at = 1; at <= events; at++) {
		setup(&m, &io, &ops);
		m.fail_event = at;
		/* Delay callbacks cannot fail; injection may hit that event. */
		error = rtw8723d_power_on(&io, &ops);
		assert(error == (m.fault_delivered ? EIO : 0));
		if (m.fault_delivered)
			assert(io.error == EIO);
		assert(m.unmasked_shutdowns == 0);
	}
	setup(&m, &io, &ops);
	store32(m.mac + 0xf0, 0x0000f000);
	assert(rtw8723d_power_on(&io, &ops) == ENOTSUP);
	assert(m.writes == 0);
	setup(&m, &io, &ops);
	m.stuck_power = true;
	assert(rtw8723d_power_on(&io, &ops) == ETIMEDOUT);
	assert(m.delay_us == 1000000 && m.delay_calls <= 20002);
	setup(&m, &io, &ops);
	m.cancel_enabled = true;
	assert(rtw8723d_power_on(&io, &ops) == ECANCELED);
	assert(m.events == 0);
	/* Release is bounded and must still run after cancellation. */
	setup(&m, &io, &ops);
	m.cancel_enabled = true;
	m.mac[0x100] = 0xff; m.mac[0x101] = 2;
	assert(rtw8723d_power_off(&io, &ops) == 0);
	assert(m.events != 0 && (m.local[0x86] & 3) == 1);
	setup(&m, &io, &ops);
	assert(rtw8723d_power_off(&io, &ops) == 0);
	assert(m.writes == 0); /* Already off must not rerun the power table. */
	puts("identity, chip power, bounded timeout and cleanup cancellation: PASS");
}

static void
assert_stopped(const struct model *m)
{

	assert((m->mac[0x80] & 1) == 0);
	assert((m->mac[0x03] & 4) == 0);
}

static void
firmware_boot(void)
{
	uint8_t payload[8193];
	struct model m;
	struct rtw88_io io;
	struct rtw8723d_boot_ops ops;
	struct rtw8723d_fw fw = { .payload = payload, .payload_size = sizeof(payload) };
	unsigned events, at;
	size_t i;
	int error;

	for (i = 0; i < sizeof(payload); i++)
		payload[i] = (uint8_t)(i * 17 + 3);
	setup(&m, &io, &ops);
	m.expected = 8196;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == 0);
	assert(memcmp(m.image, payload, sizeof(payload)) == 0);
	assert(m.image[8193] == 0 && m.image[8194] == 0 && m.image[8195] == 0);
	assert(m.windows == 2049 && ((load32(m.mac + 0x80) >> 16) & 7) == 2);
	assert((m.mac[0x80] & 0xc7) == 0xc6);
	assert((m.mac[0x03] & 4) != 0);
	/* A tiny image exercises every failure/cancel boundary quickly. */
	fw.payload_size = 3;
	setup(&m, &io, &ops);
	m.expected = 4;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == 0);
	events = m.events;
	for (at = 1; at <= events; at++) {
		setup(&m, &io, &ops);
		m.expected = 4; m.fail_event = at;
		error = rtw8723d_fw_boot(&io, &fw, &ops);
		/* Only waits/delays lack an error return. */
		assert(error == (m.fault_delivered ? EIO : 0));
		if (m.fault_delivered) {
			assert(error == EIO && io.error == EIO);
			assert_stopped(&m);
		}
	}
	for (at = 0; at <= events; at++) {
		setup(&m, &io, &ops);
		m.expected = 4; m.cancel_enabled = true; m.cancel_event = at;
		assert(rtw8723d_fw_boot(&io, &fw, &ops) == ECANCELED);
		assert(io.error == ECANCELED);
		assert_stopped(&m);
	}
	setup(&m, &io, &ops);
	m.expected = 4; m.stuck_enable = true;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == ETIMEDOUT);
	assert(m.windows == 0 && m.waits == 200);
	assert_stopped(&m);
	setup(&m, &io, &ops);
	m.expected = 4; m.stuck_report = true;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == ETIMEDOUT);
	assert(m.delay_us == 10000 && m.delay_calls == 1000);
	assert_stopped(&m);
	setup(&m, &io, &ops);
	m.expected = 4; m.stuck_ready = true;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == ETIMEDOUT);
	assert(m.waits == 200);
	assert_stopped(&m);
	setup(&m, &io, &ops);
	m.expected = 4; m.fail_window = true;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == ENOSPC);
	assert(io.error == ENOSPC);
	assert_stopped(&m);
	setup(&m, &io, &ops);
	m.expected = 4; m.fail_window = true; m.fail_cleanup = true;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == ENOSPC);
	assert(io.error == ENOSPC && m.cleanup_attempts != 0);
	setup(&m, &io, &ops);
	io.error = EAGAIN;
	assert(rtw8723d_fw_boot(&io, &fw, &ops) == EAGAIN);
	assert(m.events == 0);
	puts("firmware pages, errors, cancellation and bounded polls: PASS");
}

int
main(void)
{

	parser_contract();
	identity_and_power();
	firmware_boot();
	return 0;
}
