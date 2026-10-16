/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C MAC register model, AI-assisted. */
/*
 * Shared register model for the reference trace generator and the
 * native MAC init contract. Registers start from a fixed non-zero
 * pattern so a lost read-modify-write is visible in the trace.
 */
#ifndef RTW8822C_MAC_MODEL_H
#define RTW8822C_MAC_MODEL_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define MODEL_SIZE 0x10000
#define MODEL_TRACE_MAX 4096

static uint8_t model_reg[MODEL_SIZE];
static char model_trace[MODEL_TRACE_MAX][32];
static unsigned model_ntrace;

static void
model_reset(void)
{
	unsigned i;

	for (i = 0; i < MODEL_SIZE; i++)
		model_reg[i] = (uint8_t)(i * 37 + 11);
	/* init_h2c needs an empty H2C queue: write and read pointers equal. */
	memset(&model_reg[0x10d0], 0, 8);
	model_ntrace = 0;
}

static uint32_t
model_read(uint32_t addr, unsigned size)
{
	uint32_t v = 0;
	unsigned i;

	for (i = 0; i < size; i++)
		v |= (uint32_t)model_reg[(addr + i) & (MODEL_SIZE - 1)] << (8 * i);
	return v;
}

static void
model_write(uint32_t addr, unsigned size, uint32_t v)
{
	unsigned i;

	if (model_ntrace < MODEL_TRACE_MAX)
		snprintf(model_trace[model_ntrace++], sizeof(model_trace[0]),
		    "w%u 0x%04x 0x%0*x", size, (unsigned)addr, (int)(2 * size),
		    (unsigned)v);
	for (i = 0; i < size; i++)
		model_reg[(addr + i) & (MODEL_SIZE - 1)] =
		    (uint8_t)(v >> (8 * i));
	/* AUTO_LLT_V1 completes immediately. */
	if (addr == 0x0208)
		model_reg[0x0208] &= (uint8_t)~0x01;
}

#endif /* RTW8822C_MAC_MODEL_H */
