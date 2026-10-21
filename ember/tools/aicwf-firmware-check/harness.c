/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; original U02 instruction executor, not a firmware model. */
#include "harness.h"
#include "cases.inc"

volatile u32 mailbox[4];
volatile struct result results[CASE_COUNT];
volatile u8 frame_results[CASE_COUNT][128];
volatile u32 rng_calls, irq_count, active_context;
volatile u32 irq_snapshot[3];
static u8 frame_buffer[65536];
static u8 rx_state[64];
static u8 special;

static void
copy(volatile u8 *dst, const volatile u8 *src, u32 count)
{
	u32 index;

	for (index = 0; index < count; index++)
		dst[index] = src[index];
}

static void
write32(u32 address, u32 value)
{
	*(volatile u32 *)address = value;
}

u32
harness_rng(void)
{
	rng_calls++;
	return 0xa1b2c3d4U;
}

void
harness_irq(void)
{
	const volatile u32 *context = (const volatile u32 *)active_context;

	irq_count++;
	if (active_context != 0) {
		irq_snapshot[0] = context[0];
		irq_snapshot[1] = context[1];
		irq_snapshot[2] = *(volatile u8 *)(active_context + 0x63U);
	}
}

void
harness_assert(void)
{
	mailbox[3] = 0xbadU;
	for (;;)
		__asm volatile ("nop");
}

void
harness_main(void)
{
	const struct test_case *test;
	volatile struct result *result;
	volatile u8 *vifs = (volatile u8 *)VIF_BASE;
	struct call call;
	u32 index, offset;

	mailbox[0] = 0x55303242U;
	mailbox[1] = CASE_COUNT;
	mailbox[2] = sizeof(struct result);
	mailbox[3] = 1;
	/* Only the synthetic model IRQ31 is enabled. SysTick stays disabled. */
	write32(0xe000e180U, 0xffffffffU);
	write32(0xe000e280U, 0xffffffffU);
	write32(0xe000e100U, 0x80000000U);
	for (index = 0; index < CASE_COUNT; index++) {
		u32 context, vif, slot;

		test = &test_cases[index];
		result = &results[index];
		result->id = index;
		for (offset = 0; offset < VIF_BYTES; offset++)
			vifs[offset] = (u8)(offset * 17U + 37U);
		vif = VIF_BASE + test->vif * VIF_SIZE;
		slot = test->keyid * 0x78U + 0x1e0U;
		context = vif + slot;
		active_context = context;
		write32(context, test->floor_lo);
		write32(context + 4U, test->floor_hi);
		write32(context + 0x48U, test->tx_lo);
		write32(context + 0x4cU, test->tx_hi);
		for (offset = 0; offset < 16; offset++) {
			*(volatile u8 *)(context + 0x50U + offset) = (u8)offset;
			*(volatile u8 *)(context + 0x64U + offset) = (u8)offset;
		}
		*(volatile u8 *)(context + 0x60U) = 5;
		*(volatile u8 *)(context + 0x61U) = (u8)test->keyid;
		*(volatile u8 *)(context + 0x63U) = test->kind == 1 ? 0 : 1;
		*(volatile u8 *)(context + 0x74U) = 16;
		write32(vif + 0x4b0U, context);
		write32(vif + 0x4b4U, context);
		if (test->seed != (void *)0) {
			const struct fixture_seed *seed = test->seed;
			u32 other = vif + (test->keyid == 4 ? 5U : 4U) * 0x78U + 0x1e0U;

			*(volatile u8 *)(context + 0x60U) = (u8)seed->cipher;
			*(volatile u8 *)(context + 0x61U) = (u8)test->keyid;
			*(volatile u8 *)(context + 0x62U) = (u8)seed->hwindex;
			*(volatile u8 *)(context + 0x63U) = (u8)seed->valid;
			*(volatile u8 *)(context + 0x74U) = (u8)seed->length;
			for (offset = 0; offset < 16; offset++)
				*(volatile u8 *)(context + 0x50U + offset) = (u8)(offset ^ seed->key_xor);
			if (seed->backup_word != 0)
				*(volatile u8 *)(context + 0x64U + 4U * (seed->backup_word - 1U)) ^= 1;
			write32(vif + 0x4b0U, seed->ptk == 0 ? 0 : seed->ptk == 1 ? context : other);
			write32(vif + 0x4b4U, seed->igtk == 0 ? 0 : seed->igtk == 1 ? context : other);
			write32(0x1895acU, seed->station_flags);
		}
		copy(result->before, vifs, VIF_BYTES);
		copy(frame_buffer, test->frame, test->frame_len);
		for (offset = 0; offset < sizeof(rx_state); offset++)
			rx_state[offset] = 0;
		rx_state[0] = frame_buffer[0];
		rx_state[1] = frame_buffer[1];
		rx_state[8] = 24;
		rx_state[9] = 0;
		rx_state[10] = (u8)test->vif;
		rx_state[0x30] = 8;
		if (test->seed != (void *)0) {
			rx_state[9] = (u8)test->seed->station;
			rx_state[10] = (u8)test->seed->rx_vif;
			rx_state[0x30] = (u8)test->seed->rx_flags;
		}
		special = 0xa5;
		rng_calls = 0;
		call.args[4] = 0;
		if (test->kind == 1 || test->kind == 4) {
			call.entry = 0x142be1U;
			call.args[0] = (u32)test->request;
			call.args[1] = test->seed == (void *)0 ? 0x2aU : test->seed->incoming_hwindex;
			call.args[2] = call.args[3] = 0;
		} else if (test->kind == 2) {
			call.entry = 0x165e49U;
			call.args[0] = (u32)rx_state;
			call.args[1] = (u32)frame_buffer;
			call.args[2] = test->frame_len;
			call.args[3] = (u32)&special;
		} else {
			call.entry = 0x16651dU;
			call.args[0] = context;
			call.args[1] = (u32)frame_buffer;
			call.args[2] = test->frame_len - 18U;
			call.args[3] = 24;
		}
		__asm volatile ("msr primask, %0" : : "r"(test->mask) : "memory");
		abi_call(&call, &result->answer);
		__asm volatile ("cpsie i" : : : "memory");
		result->rng_calls = rng_calls;
		result->special = special;
		copy(result->after, vifs, VIF_BYTES);
		if (test->frame_len > 128)
			harness_assert();
		copy(frame_results[index], frame_buffer, test->frame_len);
	}
	mailbox[3] = 2;
	__asm volatile (".global harness_done\nharness_done:");
	for (;;)
		__asm volatile ("nop");
}
