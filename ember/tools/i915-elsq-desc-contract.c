/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Actual native i915 write_desc(). Model bus_space_write_4() as a log of
 * (byte offset, value) pairs. Gen11+ ELSQ has two dwords per port at
 * SQ_CONTENTS + port * 8 (lower) and + port * 8 + 4 (upper). Pre-Gen11
 * ELSP writes upper then lower to one register.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)

typedef uint32_t u32;
typedef uint64_t u64;
typedef unsigned long bus_size_t;
typedef int bus_space_tag_t;
typedef int bus_space_handle_t;
#define lower_32_bits(n) ((u32)(n))
#define upper_32_bits(n) ((u32)((n) >> 32))
#define writel(v, a) CHECK(!"Linux MMIO path selected")

struct intel_engine_execlists {
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_size_t submit_reg;
	bus_size_t ctrl_reg;
};

static struct { bus_size_t off; u32 val; } log_[16];
static unsigned nlog;

static void
bus_space_write_4(bus_space_tag_t t, bus_space_handle_t h, bus_size_t off,
    u32 val)
{
	(void)t; (void)h;
	CHECK(nlog < sizeof(log_) / sizeof(log_[0]));
	log_[nlog].off = off;
	log_[nlog].val = val;
	nlog++;
}

#define __NetBSD__ 1
#include "write-desc-body.h"

int
main(void)
{
	/* Gen11 RCS: SQ_CONTENTS 0x2510, EXECLIST_CONTROL 0x2550. */
	struct intel_engine_execlists elsq = {
		.submit_reg = 0x2510, .ctrl_reg = 0x2550,
	};
	struct intel_engine_execlists elsp = { .submit_reg = 0x2230 };
	const u64 d0 = 0x1122334455667788ull, d1 = 0x99aabbccddeeff00ull;

	/* Ports are written highest first, as execlists_submit_ports does. */
	write_desc(&elsq, d1, 1);
	write_desc(&elsq, d0, 0);
	CHECK(nlog == 4);
	CHECK(log_[0].off == 0x2518 && log_[0].val == lower_32_bits(d1));
	CHECK(log_[1].off == 0x251c && log_[1].val == upper_32_bits(d1));
	CHECK(log_[2].off == 0x2510 && log_[2].val == lower_32_bits(d0));
	CHECK(log_[3].off == 0x2514 && log_[3].val == upper_32_bits(d0));
	for (unsigned i = 0; i < nlog; i++)
		CHECK(log_[i].off % 4 == 0);

	/* Pre-Gen11 ELSP: upper then lower dword, same register. */
	nlog = 0;
	write_desc(&elsp, d0, 1);
	CHECK(nlog == 2);
	CHECK(log_[0].off == 0x2230 && log_[0].val == upper_32_bits(d0));
	CHECK(log_[1].off == 0x2230 && log_[1].val == lower_32_bits(d0));

	printf("i915 ELSQ descriptor contract: %u checks passed\n", checks);
	return 0;
}
