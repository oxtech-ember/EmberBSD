/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: RTL8822C LTE register model for native/reference consumers. */
#ifndef RTW8822C_COEX_MODEL_H
#define RTW8822C_COEX_MODEL_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define COEX_TRACE_MAX 4096
static uint8_t regfile[65536];
static uint32_t indirect[256];
static char trace[COEX_TRACE_MAX][64];
static unsigned ntrace, operations, fail_at, after_error, delay_total;
static unsigned ready_reads, fail_ready_at, ready_after;
static int failed, stuck;
static void
record(char kind, unsigned size, uint32_t addr, uint32_t val)
{
	if (ntrace == COEX_TRACE_MAX)
		abort();
	snprintf(trace[ntrace++], sizeof(trace[0]), "%c%u %04x %08x",
	    kind, size, (unsigned)addr, (unsigned)val);
}
static uint32_t
peek(uint32_t addr, unsigned size)
{
	uint32_t value = 0;
	unsigned i;
	for (i = 0; i < size; i++)
		value |= (uint32_t)regfile[addr + i] << (8 * i);
	return value;
}
static void
poke(uint32_t addr, unsigned size, uint32_t value)
{
	unsigned i;
	for (i = 0; i < size; i++)
		regfile[addr + i] = (uint8_t)(value >> (8 * i));
}
static void
model_reset(void)
{
	unsigned i;
	for (i = 0; i < sizeof(regfile); i++)
		regfile[i] = (uint8_t)(i * 37 + 11);
	for (i = 0; i < 256; i++)
		indirect[i] = UINT32_C(0x89abcdef) ^ (i * UINT32_C(0x12345));
	ntrace = operations = fail_at = after_error = delay_total = 0;
	ready_reads = fail_ready_at = ready_after = 0;
	failed = stuck = 0;
}
static int
operation(void)
{
	if (failed)
		after_error++;
	operations++;
	if (fail_at != 0 && operations == fail_at) {
		failed = 1;
		return 1;
	}
	return 0;
}
static uint32_t
model_read(uint32_t addr, unsigned size)
{
	uint32_t v = peek(addr, size);
	if (addr == 0x1700 && size == 4) {
		ready_reads++;
		v = (v & ~UINT32_C(0x20000000)) |
		    ((stuck || (fail_ready_at && ready_reads >= fail_ready_at) ||
		    ready_reads < ready_after) ? 0 : UINT32_C(0x20000000));
	}
	record('r', size, addr, v);
	return v;
}
static void
model_write(uint32_t addr, unsigned size, uint32_t value)
{
	record('w', size, addr, value);
	poke(addr, size, value);
	if (addr == 0x1700 && size == 4) {
		unsigned offset = value & 0xff;
		if ((value & UINT32_C(0xc0000000)) == UINT32_C(0x80000000))
			poke(0x1708, 4, indirect[offset]);
		if ((value & UINT32_C(0xc0000000)) == UINT32_C(0xc0000000))
			indirect[offset] = peek(0x1704, 4);
	}
}
static void
model_delay(unsigned us)
{
	delay_total += us;
	record('d', 0, 0, us);
}
#endif
