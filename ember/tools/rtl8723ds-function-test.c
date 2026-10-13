/* Origin: EmberBSD - check real function lifecycle against a CMD52 card model. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "rtl8723ds_function.h"

struct card {
	uint8_t enable;
	unsigned int calls, waits, writes, fail_at, ready_after;
	uint32_t r5;
	bool lose_restore;
};

static int
command(void *cookie, uint32_t arg, uint32_t *response)
{
	struct card *card = cookie;
	unsigned int address = (arg >> 9) & 0x1ffff;
	bool write = (arg & 0x80000000U) != 0;

	/* No RAW, reserved bits, other functions, or radio-register writes. */
	assert((arg & 0x7c000100U) == 0);
	assert(address == 2 || (address == 3 && !write));
	card->calls++;
	if (write) {
		card->writes++;
		if (!(card->lose_restore && card->writes > 1))
			card->enable = arg & 0xff;
	}
	/* A write may reach the card even when its response is lost. */
	if (card->calls == card->fail_at)
		return ETIMEDOUT;
	*response = 0x2000 | card->r5;
	if (address == 2)
		*response |= card->enable;
	else if (card->waits >= card->ready_after)
		*response |= 2;
	return 0;
}

static void
wait_ms(void *cookie, unsigned int ms)
{
	struct card *card = cookie;
	assert(ms == 10);
	card->waits++;
}

int
main(void)
{
	struct card card = { .enable = 0x10, .ready_after = 3 };
	struct rtl8723ds_function_ops ops = { command, wait_ms, &card };
	struct rtl8723ds_function_state state;
	static const uint32_t r5_errors[] = { 0x8000, 0x4000, 0x800, 0x200, 0x100 };
	static const uint32_t bad_writes[] = {
		0x80000602, 0x90000402, 0x88000402, 0x80000502,
		0x84000402, 0x80080402, 0x80000002
	};

	assert(rtl8723ds_function_write_allowed(true, 0x80000402));
	assert(rtl8723ds_function_write_allowed(true, 0x80000400));
	assert(!rtl8723ds_function_write_allowed(false, 0x80000402));
	for (unsigned int i = 0; i < sizeof(bad_writes) / sizeof(bad_writes[0]); i++)
		assert(!rtl8723ds_function_write_allowed(true, bad_writes[i]));

	assert(rtl8723ds_function_enable(&ops, &state) == 0);
	assert(card.enable == 0x12 && state.changed && state.ready == 2);
	assert(card.waits == 3);
	assert(rtl8723ds_function_restore(&ops, &state) == 0);
	assert(card.enable == 0x10 && !state.changed);
	assert(rtl8723ds_function_restore(&ops, &state) == 0);
	assert(card.writes == 2);
	puts("function ready and checked restore preserve other functions");

	memset(&card, 0, sizeof(card)); card.enable = 2;
	assert(rtl8723ds_function_enable(&ops, &state) == EBUSY);
	assert(card.writes == 0 && !state.changed);
	for (unsigned int fail = 1; fail <= 4; fail++) {
		memset(&card, 0, sizeof(card)); card.fail_at = fail;
		assert(rtl8723ds_function_enable(&ops, &state) == ETIMEDOUT);
		assert(card.writes == (fail == 1 ? 0U : 1U));
		assert(rtl8723ds_function_restore(&ops, &state) == 0);
		assert(card.enable == 0 && !state.changed);
	}
	puts("host errors, including ambiguous writes, retain rollback state");

	memset(&card, 0, sizeof(card)); card.ready_after = 101;
	assert(rtl8723ds_function_enable(&ops, &state) == ETIMEDOUT);
	assert(card.waits == 100);
	assert(rtl8723ds_function_restore(&ops, &state) == 0);
	assert(card.enable == 0);
	for (unsigned int i = 0; i < sizeof(r5_errors) / sizeof(r5_errors[0]); i++) {
		memset(&card, 0, sizeof(card)); card.r5 = r5_errors[i];
		assert(rtl8723ds_function_enable(&ops, &state) != 0);
		assert(card.writes == 0 && !state.changed);
	}
	puts("card errors stop before writes; ready timeout is bounded");

	memset(&card, 0, sizeof(card)); card.lose_restore = true;
	assert(rtl8723ds_function_enable(&ops, &state) == 0);
	assert(rtl8723ds_function_restore(&ops, &state) == EIO);
	assert(state.changed);
	card.lose_restore = false;
	assert(rtl8723ds_function_restore(&ops, &state) == 0);
	assert(rtl8723ds_function_enable(NULL, &state) == EINVAL);
	assert(rtl8723ds_function_enable(&ops, NULL) == EINVAL);
	ops.wait_ms = NULL;
	assert(rtl8723ds_function_enable(&ops, &state) == EINVAL);
	puts("restore mismatch is reported and can be retried; bad callbacks rejected");
	return 0;
}
