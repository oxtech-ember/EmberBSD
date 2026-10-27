/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise production SDIO helpers; model only the command boundary. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dev/sdmmc/rtw88_sdio_io.h>

static struct card {
	uint8_t bytes[2][0x20000];
	uint8_t scratch[4096];
	uint32_t args[5000];
	unsigned int calls, direct, extended, writes, waits, ready_reads;
	unsigned int ready_after, fail_at, r5_at, block_size;
	uint32_t r5;
	bool ambiguous;
} card;

static int
command52(void *cookie, uint32_t arg, uint32_t *response)
{
	struct card *c = cookie;
	unsigned int fn = (arg >> 28) & 7;
	unsigned int address = (arg >> 9) & 0x1ffff;
	bool write = (arg & 0x80000000U) != 0;

	assert(fn <= 1 && (arg & 0x0c000100U) == 0);
	assert(c->calls < 5000);
	c->args[c->calls++] = arg;
	c->direct++;
	if (c->calls == c->fail_at && !(write && c->ambiguous))
		return ETIMEDOUT;
	if (write) {
		c->bytes[fn][address] = arg & 0xff;
		c->writes++;
	}
	if (!write && fn == 0 && address == 3) {
		c->ready_reads++;
		if (c->ready_after != 0 && c->ready_reads >= c->ready_after)
			c->bytes[0][3] |= 2;
	}
	*response = c->bytes[fn][address];
	if (c->calls == c->r5_at)
		*response |= c->r5;
	return c->calls == c->fail_at ? ETIMEDOUT : 0;
}

static int
command53(void *cookie, uint32_t arg, void *data, size_t len, uint32_t *response)
{
	struct card *c = cookie;
	unsigned int fn = (arg >> 28) & 7;
	unsigned int address = (arg >> 9) & 0x1ffff;
	unsigned int count = arg & 0x1ff;
	bool block = (arg & 0x08000000U) != 0;
	bool write = (arg & 0x80000000U) != 0;

	assert(fn == 1 && (arg & 0x04000000U) != 0);
	assert(len <= 512 && address + len <= 0x20000);
	assert(len == (block ? count * c->block_size : (count ? count : 512)));
	assert(!block || count != 0);
	assert(c->calls < 5000);
	c->args[c->calls++] = arg;
	c->extended++;
	if (write) {
		c->writes++;
		memcpy(&c->bytes[1][address], data, len);
	} else {
		/* A failed host command can still change its private DMA buffer. */
		memcpy(data, &c->bytes[1][address], len);
	}
	*response = c->calls == c->r5_at ? c->r5 : 0;
	return c->calls == c->fail_at ? ETIMEDOUT : 0;
}

static void
wait_ms(void *cookie, unsigned int ms)
{
	struct card *c = cookie;
	assert(ms == 10);
	c->waits++;
}

static struct rtw88_sdio_ops ops = {
	.command52 = command52, .command53 = command53,
	.wait_ms = wait_ms, .cookie = &card,
};

static const struct {
	uint32_t flags;
	int error;
} r5_cases[] = {
	{ 0x8000, EILSEQ }, { 0x4000, EIO }, { 0x0800, EIO },
	{ 0x0200, EIO }, { 0x0100, EIO },
};

static struct rtw88_sdio_bus
fresh_bus(void)
{
	struct rtw88_sdio_bus bus = {
		.ops = &ops, .block_size = 512, .max_request = 512,
		.read_scratch = card.scratch, .read_scratch_len = sizeof(card.scratch),
	};
	memset(&card, 0, sizeof(card));
	card.ready_after = 1;
	card.block_size = 512;
	return bus;
}

static void
cmd52_contract(void)
{
	struct rtw88_sdio_bus bus = fresh_bus();
	uint8_t value = 0x5a;
	int error;

	card.bytes[1][0x1ffff] = 0xa5;
	error = rtw88_sdio_cmd52(bus.ops, 1, 0x1ffff, false, &value);
	assert(error == 0 && value == 0xa5 && card.args[0] == 0x13fffe00U);
	for (unsigned int i = 0; i < 5; i++) {
		bus = fresh_bus();
		card.r5_at = 1;
		card.r5 = r5_cases[i].flags;
		value = 0x5a;
		error = rtw88_sdio_cmd52(bus.ops, 0, 2, false, &value);
		assert(error == r5_cases[i].error && value == 0x5a);
	}
	bus = fresh_bus();
	error = rtw88_sdio_cmd52(bus.ops, 2, 0, false, &value);
	assert(error == EINVAL && card.calls == 0);
	error = rtw88_sdio_cmd52(bus.ops, 1, 0x20000, false, &value);
	assert(error == EINVAL && card.calls == 0);
	error = rtw88_sdio_cmd52(NULL, 0, 2, false, &value);
	assert(error == EINVAL && card.calls == 0);
	card.fail_at = 1;
	value = 0x5a;
	error = rtw88_sdio_cmd52(bus.ops, 0, 2, false, &value);
	assert(error == ETIMEDOUT && value == 0x5a);
}

static void
transfer_contract(void)
{
	struct rtw88_sdio_bus bus = fresh_bus();
	uint8_t data[513], before[513];
	int error;

	memset(data, 0x5a, sizeof(data));
	memcpy(before, data, sizeof(data));
	card.bytes[1][0xfff] = 0xa5;
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_LOCAL, 0xfff, data, 1, false);
	assert(error == 0 && data[0] == 0xa5 && card.args[0] == 0x101ffe00);
	unsigned int n = card.calls;
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_LOCAL, 0xfff, data, 2, false);
	assert(error == EINVAL && card.calls == n);
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0xffff, data, 2, false);
	assert(error == EINVAL && card.calls == n);
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0, data, 0, false);
	assert(error == EINVAL && card.calls == n);
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0, data, 4097, false);
	assert(error != 0 && card.calls == n);
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0,
	    card.scratch, 4, false);
	assert(error == EINVAL && card.calls == n);
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0,
	    card.scratch + 1, 4, false);
	assert(error == EINVAL && card.calls == n);
	bus.read_scratch = card.scratch + 1;
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0,
	    card.scratch, 4, false);
	assert(error == EINVAL && card.calls == n);
	bus.read_scratch = card.scratch;
	bus.read_scratch_len = 3;
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0, data, 4, false);
	assert(error == EINVAL && card.calls == n);

	bus = fresh_bus();
	memcpy(data, before, sizeof(data));
	card.bytes[1][0x10000] = 0xa5;
	card.fail_at = 2;
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0, data, 4, false);
	assert(error == ETIMEDOUT && memcmp(data, before, sizeof(data)) == 0);
	assert(card.calls == 2);

	bus = fresh_bus();
	error = rtw88_sdio_set_cmd53(&bus, true);
	assert(error == EPROTO && card.calls == 0);
	bus.function_claimed = bus.function_ready = true;
	error = rtw88_sdio_set_cmd53(&bus, true);
	assert(error == 0 && card.calls == 0);
	for (unsigned int i = 0; i < 513; i++)
		card.bytes[1][0x11000 + i] = (uint8_t)i;
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0x1000, data, 513, false);
	assert(error == 0 && card.extended == 2 && card.direct == 0);
	assert(card.args[0] == 0x1e200001 && card.args[1] == 0x16240001);
	for (unsigned int i = 0; i < 513; i++)
		assert(data[i] == (uint8_t)i);

	bus = fresh_bus();
	bus.function_claimed = bus.function_ready = true;
	bus.block_size = card.block_size = 1024;
	error = rtw88_sdio_set_cmd53(&bus, true);
	assert(error == 0);
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0x1000, data, 512, true);
	assert(error == 0 && card.args[0] == 0x96200000U);
	assert(card.extended == 1 && card.writes == 1);

	bus = fresh_bus();
	bus.function_claimed = bus.function_ready = true;
	error = rtw88_sdio_set_cmd53(&bus, true);
	assert(error == 0);
	card.fail_at = 2;
	memcpy(data, before, sizeof(data));
	error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0x1000, data, 513, false);
	assert(error == ETIMEDOUT && card.extended == 2);
	assert(memcmp(data, before, sizeof(data)) == 0);
	for (unsigned int i = 0; i < 5; i++) {
		bus = fresh_bus();
		bus.function_claimed = bus.function_ready = true;
		error = rtw88_sdio_set_cmd53(&bus, true);
		assert(error == 0);
		card.r5_at = 1;
		card.r5 = r5_cases[i].flags;
		memcpy(data, before, sizeof(data));
		error = rtw88_sdio_transfer(&bus, RTW88_SDIO_MAC, 0, data, 4, false);
		assert(error == r5_cases[i].error && card.extended == 1);
		assert(memcmp(data, before, sizeof(data)) == 0);
	}
}

static void
function_contract(void)
{
	struct rtw88_sdio_bus bus = fresh_bus();
	struct rtw88_sdio_function_state state;
	int error;

	card.bytes[0][2] = 0x10;
	error = rtw88_sdio_function_acquire(&bus, &state, true);
	assert(error == 0 && state.changed && state.owns_enable);
	assert(bus.function_ready && card.bytes[0][2] == 0x12);
	card.bytes[0][2] |= 0x20; /* Another function changed after acquisition. */
	error = rtw88_sdio_function_release(&bus, &state, RTW88_SDIO_RESTORE_OWNED_BIT);
	assert(error == 0 && card.bytes[0][2] == 0x30 && !state.changed);
	assert(!bus.function_ready && !bus.cmd53_enabled);
	unsigned int n = card.calls;
	error = rtw88_sdio_function_release(&bus, &state, RTW88_SDIO_RESTORE_OWNED_BIT);
	assert(error == 0 && card.calls == n);

	bus = fresh_bus();
	card.bytes[0][2] = 0x12;
	error = rtw88_sdio_function_acquire(&bus, &state, false);
	assert(error == EBUSY && !state.changed && card.writes == 0);
	error = rtw88_sdio_function_acquire(&bus, &state, true);
	assert(error == 0 && !state.owns_enable && !state.changed);
	error = rtw88_sdio_function_release(&bus, &state, RTW88_SDIO_RESTORE_OWNED_BIT);
	assert(error == 0 && card.writes == 0 && card.bytes[0][2] == 0x12);

	bus = fresh_bus();
	card.fail_at = 2;
	card.ambiguous = true;
	error = rtw88_sdio_function_acquire(&bus, &state, true);
	assert(error == ETIMEDOUT && state.changed && card.bytes[0][2] == 2);
	error = rtw88_sdio_function_release(&bus, &state, RTW88_SDIO_RESTORE_OWNED_BIT);
	assert(error == 0 && card.bytes[0][2] == 0);

	bus = fresh_bus();
	card.ready_after = 0;
	error = rtw88_sdio_function_acquire(&bus, &state, true);
	assert(error == ETIMEDOUT && state.changed);
	assert(card.ready_reads == 101 && card.waits == 100);
	error = rtw88_sdio_function_release(&bus, &state, RTW88_SDIO_RESTORE_OWNED_BIT);
	assert(error == 0);

	bus = fresh_bus();
	card.bytes[0][2] = 0x10;
	error = rtw88_sdio_function_acquire(&bus, &state, false);
	assert(error == 0);
	card.bytes[0][2] = 0x32;
	error = rtw88_sdio_function_release(&bus, &state,
	    RTW88_SDIO_RESTORE_DIAGNOSTIC_SNAPSHOT);
	assert(error == 0 && card.bytes[0][2] == 0x10);
}

int
main(void)
{
	cmd52_contract();
	transfer_contract();
	function_contract();
	puts("rtw88 SDIO I/O and function contract: PASS");
	return 0;
}
