/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#ifdef _KERNEL
#include <sys/null.h>
#else
#include <stddef.h>
#endif

#include "cixvpu_resources.h"

/* Both CIXH3010 register windows require at least 64 KiB. */
#define CIXVPU_RESOURCE_MIN_SPAN 0x10000U

int
cixvpu_acpi_resource_type_allowed(uint32_t type)
{

	return type == CIXVPU_ACPI_FIXED_MEMORY32 ||
	    type == CIXVPU_ACPI_EXTENDED_IRQ || type == CIXVPU_ACPI_END_TAG;
}

enum cixvpu_resource_status
cixvpu_resources_validate(const struct cixvpu_resources *res)
{
	uint64_t end[2], xend[2];
	unsigned int i;

	if (res == NULL || res->nmem != 2 || res->nirq != 1 || res->other)
		return CIXVPU_RESOURCE_COUNT;
	for (i = 0; i < 2; i++) {
		const struct cixvpu_mem_resource *mem = &res->mem[i];

		if (mem->length < CIXVPU_RESOURCE_MIN_SPAN ||
		    ((mem->base | mem->translated | mem->length) & 3) != 0 ||
		    mem->translated < mem->base ||
		    mem->length - 1 > UINT64_MAX - mem->base ||
		    mem->length - 1 > UINT64_MAX - mem->translated)
			return CIXVPU_RESOURCE_MEMORY;
		end[i] = mem->base + (mem->length - 1);
		xend[i] = mem->translated + (mem->length - 1);
	}
	if ((res->mem[0].base <= end[1] && res->mem[1].base <= end[0]) ||
	    (res->mem[0].translated <= xend[1] &&
	    res->mem[1].translated <= xend[0]))
		return CIXVPU_RESOURCE_OVERLAP;
	/* Accept standard GIC SPIs and ACPI level-sensitive triggering only. */
	if (res->irq < 32 || res->irq > 1019 || res->irq_trigger != 0)
		return CIXVPU_RESOURCE_IRQ;
	if (!res->cca_present || res->cca > 1)
		return CIXVPU_RESOURCE_CCA;
	return CIXVPU_RESOURCE_OK;
}
