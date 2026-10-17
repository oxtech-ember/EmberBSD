/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: deterministic register model for power setup source contracts, AI-assisted. */
#ifndef RTW8822C_POWER_MODEL_H
#define RTW8822C_POWER_MODEL_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t registers[65536];
static char events[64][64];
static unsigned event_count;
static void
model_reset(unsigned seed, unsigned flash)
{
	unsigned i;

	for (i = 0; i < sizeof(registers); i++)
		registers[i] = seed == 0 ? 0 : seed == 1 ? 0xff :
		    (uint8_t)(i * 37 + 11);
	registers[0x82] = (uint8_t)((registers[0x82] & ~0x10U) | flash * 0x10U);
	event_count = 0;
}
static void
record(char kind, uint32_t address, unsigned width, uint32_t value)
{

	if (address > sizeof(registers) - width || event_count == 64)
		abort();
	(void)snprintf(events[event_count++], sizeof(events[0]),
	    "%c%u %04x %08x", kind, width, (unsigned)address, (unsigned)value);
}
static uint32_t
model_read(uint32_t address, unsigned width)
{
	uint32_t value = 0;
	unsigned i;

	if (address > sizeof(registers) - width)
		abort();
	for (i = 0; i < width; i++)
		value |= (uint32_t)registers[address + i] << (i * 8);
	record('r', address, width, value);
	return value;
}
static void
model_write(uint32_t address, unsigned width, uint32_t value)
{
	unsigned i;

	record('w', address, width, value);
	for (i = 0; i < width; i++)
		registers[address + i] = (uint8_t)(value >> (i * 8));
}
#endif
