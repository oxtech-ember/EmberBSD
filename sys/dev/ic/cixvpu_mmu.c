/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */

#ifdef _KERNEL
#include <sys/null.h>
#include <sys/errno.h>
#else
#include <errno.h>
#endif

#include "cixvpu_mmu.h"

/* The wire format uses a 40-bit bus address and two permission bits. */
static uint32_t
cixvpu_pte_load(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void
cixvpu_pte_store(uint8_t *p, uint32_t value)
{

	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

int
cixvpu_mmu_encode(uint64_t bus, unsigned int attribute,
    unsigned int permission, uint32_t *entry)
{

	if (entry == NULL || (bus & (CIXVPU_MMU_PAGE_BYTES - 1)) != 0 ||
	    bus >= (UINT64_C(1) << 40) || attribute > 3 || permission < 1 ||
	    permission > 3)
		return EINVAL;
	*entry = (uint32_t)(bus >> 12) << 2 | attribute << 30 | permission;
	return 0;
}

int
cixvpu_mmu_lookup(const void *table, size_t bytes, unsigned int index,
    uint32_t *entry)
{

	if (table == NULL || entry == NULL ||
	    bytes != CIXVPU_MMU_PAGE_BYTES || index >= CIXVPU_MMU_ENTRIES)
		return EINVAL;
	*entry = cixvpu_pte_load((const uint8_t *)table + index * 4);
	return 0;
}

static int
cixvpu_mmu_install(void *table, size_t bytes, unsigned int index,
    uint64_t bus, unsigned int attribute, unsigned int permission)
{
	uint32_t old, entry;
	int error;

	error = cixvpu_mmu_encode(bus, attribute, permission, &entry);
	if (error != 0)
		return error;
	error = cixvpu_mmu_lookup(table, bytes, index, &old);
	if (error != 0)
		return error;
	/* Zero is the only empty entry. Do not overwrite a malformed entry. */
	if (old != 0)
		return EEXIST;
	cixvpu_pte_store((uint8_t *)table + index * 4, entry);
	return 0;
}

int
cixvpu_mmu_link(void *table, size_t bytes, uint32_t address, uint64_t bus)
{

	if ((address & ((UINT32_C(1) << 22) - 1)) != 0)
		return EINVAL;
	return cixvpu_mmu_install(table, bytes, address >> 22, bus,
	    CIXVPU_MMU_PRIVATE, CIXVPU_MMU_READ);
}

int
cixvpu_mmu_map(void *table, size_t bytes, uint32_t address, uint64_t bus,
    unsigned int attribute, unsigned int permission)
{

	if ((address & (CIXVPU_MMU_PAGE_BYTES - 1)) != 0)
		return EINVAL;
	return cixvpu_mmu_install(table, bytes,
	    (address >> 12) & (CIXVPU_MMU_ENTRIES - 1), bus, attribute,
	    permission);
}

int
cixvpu_mmu_remove(void *table, size_t bytes, unsigned int index,
    uint32_t expected)
{
	uint32_t old;
	int error;

	if ((expected & 3) == 0)
		return EINVAL;
	error = cixvpu_mmu_lookup(table, bytes, index, &old);
	if (error != 0)
		return error;
	if (old == 0)
		return ENOENT;
	if (old != expected)
		return ESTALE;
	cixvpu_pte_store((uint8_t *)table + index * 4, 0);
	return 0;
}
