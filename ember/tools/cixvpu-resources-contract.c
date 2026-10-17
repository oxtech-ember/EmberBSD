/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#include <stdio.h>
#include <stdlib.h>

#include "cixvpu_resources.h"

static unsigned int assertions;

static void
check(const char *name, const struct cixvpu_resources *res,
    enum cixvpu_resource_status expected)
{
	enum cixvpu_resource_status actual;

	actual = cixvpu_resources_validate(res);
	assertions++;
	if (actual != expected) {
		fprintf(stderr, "%s: expected %d, got %d\n", name,
		    expected, actual);
		exit(EXIT_FAILURE);
	}
}

int
main(void)
{
	/* Synthetic addresses test relocation, not a physical address allowlist. */
	const struct cixvpu_resources good = {
		.mem = {{0x100000, 0x100000, 0x10000},
		    {0x200000, 0x200000, 0x10000}},
		.nmem = 2, .nirq = 1, .irq = 358, .irq_trigger = 0,
		.cca_present = 1, .cca = 0
	};
	struct cixvpu_resources res;
	unsigned int i;

	check("missing description", NULL, CIXVPU_RESOURCE_COUNT);
	check("valid noncoherent", &good, CIXVPU_RESOURCE_OK);
	for (i = 0; i < 4; i++) {
		res = good;
		res.nmem = i;
		check("memory count", &res, i == 2 ? CIXVPU_RESOURCE_OK :
		    CIXVPU_RESOURCE_COUNT);
		res = good;
		res.nirq = i;
		check("interrupt count", &res, i == 1 ? CIXVPU_RESOURCE_OK :
		    CIXVPU_RESOURCE_COUNT);
	}
	res = good;
	res.other = 1;
	check("unexpected resources", &res, CIXVPU_RESOURCE_COUNT);
	for (i = 0; i < 2; i++) {
		res = good;
		res.mem[i].length = 0;
		check("zero span", &res, CIXVPU_RESOURCE_MEMORY);
		res.mem[i].length = 0xfffc;
		check("short span", &res, CIXVPU_RESOURCE_MEMORY);
		res.mem[i].length = 0x10001;
		check("unaligned span", &res, CIXVPU_RESOURCE_MEMORY);
		res = good;
		res.mem[i].base++;
		check("unaligned bus base", &res, CIXVPU_RESOURCE_MEMORY);
		res = good;
		res.mem[i].translated++;
		check("unaligned CPU base", &res, CIXVPU_RESOURCE_MEMORY);
		res = good;
		res.mem[i].base = UINT64_MAX - 0xfffb;
		res.mem[i].translated = res.mem[i].base;
		check("bus span overflow", &res, CIXVPU_RESOURCE_MEMORY);
		res = good;
		res.mem[i].translated = UINT64_MAX - 0xfffb;
		check("CPU span overflow", &res, CIXVPU_RESOURCE_MEMORY);
		res = good;
		res.mem[i].translated = res.mem[i].base - 4;
		check("translation overflow", &res, CIXVPU_RESOURCE_MEMORY);
	}
	res = good;
	res.mem[1].base = res.mem[0].base + 0xfffc;
	check("bus overlap", &res, CIXVPU_RESOURCE_OVERLAP);
	res = good;
	res.mem[0].translated = 0x200000;
	check("CPU overlap", &res, CIXVPU_RESOURCE_OVERLAP);
	res = good;
	res.mem[1].base = res.mem[1].translated = 0x110000;
	check("adjacent spans", &res, CIXVPU_RESOURCE_OK);
	res = good;
	res.mem[0] = good.mem[1];
	res.mem[1] = good.mem[0];
	check("descending disjoint spans", &res, CIXVPU_RESOURCE_OK);
	res = good;
	res.mem[1].base = res.mem[1].translated = UINT64_MAX - 0xffff;
	check("last representable byte", &res, CIXVPU_RESOURCE_OK);
	res = good;
	res.mem[0].translated += 0x1000000;
	res.mem[1].translated += 0x1000000;
	check("translated disjoint spans", &res, CIXVPU_RESOURCE_OK);
	res = good;
	res.irq = 31;
	check("private interrupt", &res, CIXVPU_RESOURCE_IRQ);
	res.irq = 32;
	check("first SPI", &res, CIXVPU_RESOURCE_OK);
	res.irq = 1019;
	check("last standard SPI", &res, CIXVPU_RESOURCE_OK);
	res.irq = 1020;
	check("reserved interrupt", &res, CIXVPU_RESOURCE_IRQ);
	res.irq = UINT32_MAX;
	check("invalid interrupt", &res, CIXVPU_RESOURCE_IRQ);
	res = good;
	res.irq_trigger = 1;
	check("edge interrupt", &res, CIXVPU_RESOURCE_IRQ);
	res.irq_trigger = UINT32_MAX;
	check("invalid trigger", &res, CIXVPU_RESOURCE_IRQ);
	res = good;
	res.cca_present = 0;
	check("missing CCA", &res, CIXVPU_RESOURCE_CCA);
	res.cca_present = 1;
	res.cca = 1;
	check("coherent", &res, CIXVPU_RESOURCE_OK);
	res.cca = 2;
	check("invalid CCA", &res, CIXVPU_RESOURCE_CCA);
	res.cca = UINT64_MAX;
	check("wide invalid CCA", &res, CIXVPU_RESOURCE_CCA);
	printf("%u CIX VPU resource assertions passed\n", assertions);
	return EXIT_SUCCESS;
}
