/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/cdefs.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stdint.h>
#include <string.h>
#endif
#include "bcm2712_mmu4_tablevar.h"

/*
 * Numeric facts: Raspberry Pi Linux a1a601d4857978d810175365550c4536701df4ed,
 * drivers/iommu/bcm2712-iommu.c: table flags, 4 KiB pages and two-level PPNs.
 * This fixed encoder is an independent BSD implementation of those facts.
 */
#define MMU4_PA_LIMIT		(UINT64_C(1) << 36)
#define MMU4_VALID		0x10000000U
#define MMU4_PAGE_MASK		4095U
#define MMU4_PAGE_SHIFT		12U
#define MMU4_TABLE_WORDS		1024U

static int
mmu4_objects(const struct bcm2712_mmu4_pages *input,
    const struct bcm2712_mmu4_image *out)
{
	uintptr_t a, b;

	if (input == NULL || out == NULL)
		return EINVAL;
	a = (uintptr_t)input;
	b = (uintptr_t)out;
	if (a % _Alignof(struct bcm2712_mmu4_pages) != 0 ||
	    b % _Alignof(struct bcm2712_mmu4_image) != 0 ||
	    sizeof(*input) - 1 > UINTPTR_MAX - a ||
	    sizeof(*out) - 1 > UINTPTR_MAX - b)
		return EINVAL;
	if (a <= b ? b - a < sizeof(*input) : a - b < sizeof(*out))
		return EINVAL;
	return 0;
}

static int
mmu4_page(uint64_t pa)
{

	if ((pa & MMU4_PAGE_MASK) != 0)
		return EINVAL;
	if (pa > MMU4_PA_LIMIT - BCM2712_MMU4_PAGE_BYTES)
		return ERANGE;
	return 0;
}

static void
mmu4_entry(uint8_t *page, unsigned int index, uint64_t pa)
{
	uint32_t value;
	unsigned int offset;

	value = MMU4_VALID | (uint32_t)(pa >> MMU4_PAGE_SHIFT);
	offset = index * 4U;
	page[offset] = (uint8_t)value;
	page[offset + 1] = (uint8_t)(value >> 8);
	page[offset + 2] = (uint8_t)(value >> 16);
	page[offset + 3] = (uint8_t)(value >> 24);
}

int
bcm2712_mmu4_encode(const struct bcm2712_mmu4_pages *input,
    struct bcm2712_mmu4_image *out)
{
	uint64_t control[4];
	unsigned int i, j;
	int error;

	error = mmu4_objects(input, out);
	if (error != 0)
		return error;
	if ((input->debug & 0xfU) != 4U ||
	    ((input->debug >> 4) & 0xfU) < 6U ||
	    ((input->debug >> 8) & 0xfU) < 6U ||
	    (input->debug & 0x20000000U) == 0)
		return EOPNOTSUPP;
	control[0] = input->top_pa;
	control[1] = input->l2_pa[0];
	control[2] = input->l2_pa[1];
	control[3] = input->default_pa;
	for (i = 0; i < 4; i++) {
		if (control[i] == 0)
			return EINVAL;
		error = mmu4_page(control[i]);
		if (error != 0)
			return error;
		for (j = 0; j < i; j++) {
			if (control[i] == control[j])
				return EINVAL;
		}
	}
	for (i = 0; i < BCM2712_MMU4_FRAME_PAGES; i++) {
		error = mmu4_page(input->frame_pa[i]);
		if (error != 0)
			return error;
		for (j = 0; j < 4; j++) {
			if (input->frame_pa[i] == control[j])
				return EINVAL;
		}
	}

	/* Every fallible check precedes the first output write. */
	memset(out, 0, sizeof(*out));
	mmu4_entry(out->top, 0, input->l2_pa[0]);
	mmu4_entry(out->top, 1, input->l2_pa[1]);
	for (i = 0; i < BCM2712_MMU4_FRAME_PAGES; i++)
		mmu4_entry(out->l2[i / MMU4_TABLE_WORDS],
		    i % MMU4_TABLE_WORDS, input->frame_pa[i]);
	return 0;
}

int
bcm2712_mmu4_encode_range(const struct bcm2712_mmu4_range_pages *input,
    unsigned int slot, struct bcm2712_mmu4_range_image *out)
{
	uint64_t control[3];
	uintptr_t a, b;
	unsigned int i, j;
	int error;

	if (input == NULL || out == NULL || slot >= BCM2712_MMU4_SCANOUT_SLOTS)
		return EINVAL;
	a = (uintptr_t)input;
	b = (uintptr_t)out;
	if (a % _Alignof(struct bcm2712_mmu4_range_pages) != 0 ||
	    b % _Alignof(struct bcm2712_mmu4_range_image) != 0 ||
	    sizeof(*input) - 1 > UINTPTR_MAX - a ||
	    sizeof(*out) - 1 > UINTPTR_MAX - b ||
	    (a <= b ? b - a < sizeof(*input) : a - b < sizeof(*out)))
		return EINVAL;
	if ((input->debug & 0xfU) != 4U ||
	    ((input->debug >> 4) & 0xfU) < 6U ||
	    ((input->debug >> 8) & 0xfU) < 6U ||
	    (input->debug & 0x20000000U) == 0)
		return EOPNOTSUPP;
	control[0] = input->top_pa;
	control[1] = input->l2_pa[0];
	control[2] = input->l2_pa[1];
	for (i = 0; i < 3; i++) {
		if (control[i] == 0)
			return EINVAL;
		error = mmu4_page(control[i]);
		if (error != 0)
			return error;
		for (j = 0; j < i; j++) {
			if (control[i] == control[j])
				return EINVAL;
		}
	}
	for (i = 0; i < BCM2712_MMU4_FRAME_PAGES; i++) {
		error = mmu4_page(input->frame_pa[i]);
		if (error != 0)
			return error;
		for (j = 0; j < 3; j++) {
			if (input->frame_pa[i] == control[j])
				return EINVAL;
		}
	}

	memset(out, 0, sizeof(*out));
	mmu4_entry(out->top, 0, input->l2_pa[0]);
	mmu4_entry(out->top, 1, input->l2_pa[1]);
	for (i = 0; i < BCM2712_MMU4_FRAME_PAGES; i++)
		mmu4_entry(out->l2[i / MMU4_TABLE_WORDS],
		    i % MMU4_TABLE_WORDS, input->frame_pa[i]);
	return 0;
}
