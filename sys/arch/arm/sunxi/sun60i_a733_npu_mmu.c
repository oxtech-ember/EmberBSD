/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent VIP MMU32 data encoding. */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>

#include <arm/sunxi/sun60i_a733_npu_mmu.h>

#define MMU_PAGE_BYTES 4096
#define MMU_PA_LIMIT UINT64_C(0x100000000)
#define MMU_EXCEPTION 2

struct mmu_span {
	uintptr_t first, end;
};

/* Reject wrap before adding a byte count. Inputs are trusted kernel pointers. */
static bool
mmu_span(const void *pointer, size_t bytes, struct mmu_span *out)
{
	uintptr_t first = (uintptr_t)pointer;

	if (pointer == NULL || bytes > UINTPTR_MAX - first)
		return false;
	out->first = first;
	out->end = first + bytes;
	return true;
}

static bool
mmu_overlap(const struct mmu_span *a, const struct mmu_span *b)
{
	return a->first < b->end && b->first < a->end;
}

static bool
mmu_physical(uint64_t address, size_t bytes)
{
	return (address & (MMU_PAGE_BYTES - 1)) == 0 &&
	    address <= MMU_PA_LIMIT - bytes;
}

static bool
mmu_physical_overlap(uint64_t a, size_t na, uint64_t b, size_t nb)
{
	/* Each range has passed mmu_physical; both sums are at most 4 GiB. */
	return a < b + nb && b < a + na;
}

static void
mmu_put32(uint8_t *bytes, uint32_t word)
{
	/* The wire image is little-endian on every host. */
	for (unsigned int i = 0; i < 4; i++)
		bytes[i] = (uint8_t)(word >> (8 * i));
}

int
sun60i_a733_npu_mmu_build(const struct sun60i_a733_npu_mmu_plan *plan,
    void *mtlb, size_t mtlb_bytes, void *stlb, size_t stlb_bytes)
{
	struct sun60i_a733_npu_mmu_plan in;
	struct sun60i_a733_npu_mmu_page pages[A733_NPU_MMU_MAX_PAGES];
	struct mmu_span plan_span, page_span, mtlb_span, stlb_span;
	uint32_t first;

	if (!mmu_span(plan, sizeof(in), &plan_span) ||
	    mtlb_bytes < A733_NPU_MMU_MTLB_BYTES ||
	    stlb_bytes < A733_NPU_MMU_STLB_BYTES ||
	    !mmu_span(mtlb, A733_NPU_MMU_MTLB_BYTES, &mtlb_span) ||
	    !mmu_span(stlb, A733_NPU_MMU_STLB_BYTES, &stlb_span) ||
	    mmu_overlap(&mtlb_span, &stlb_span) ||
	    mmu_overlap(&plan_span, &mtlb_span) ||
	    mmu_overlap(&plan_span, &stlb_span))
		return EINVAL;
	memcpy(&in, plan, sizeof(in));
	if (in.page_count == 0 || in.page_count > A733_NPU_MMU_MAX_PAGES ||
	    !mmu_span(in.pages, in.page_count * sizeof(pages[0]), &page_span) ||
	    mmu_overlap(&page_span, &mtlb_span) ||
	    mmu_overlap(&page_span, &stlb_span))
		return EINVAL;
	first = (in.virtual_base >> 12) & 4095;
	if ((in.virtual_base & (MMU_PAGE_BYTES - 1)) != 0 ||
	    first == 0 || in.page_count > 4095 - first ||
	    !mmu_physical(in.mtlb_physical, MMU_PAGE_BYTES) ||
	    !mmu_physical(in.stlb_physical, A733_NPU_MMU_STLB_BYTES) ||
	    mmu_physical_overlap(in.mtlb_physical, MMU_PAGE_BYTES,
	    in.stlb_physical, A733_NPU_MMU_STLB_BYTES))
		return EINVAL;
	memcpy(pages, in.pages, in.page_count * sizeof(pages[0]));
	for (size_t i = 0; i < in.page_count; i++) {
		if ((pages[i].flags & ~A733_NPU_MMU_WRITABLE) != 0 ||
		    !mmu_physical(pages[i].physical, MMU_PAGE_BYTES) ||
		    mmu_physical_overlap(pages[i].physical, MMU_PAGE_BYTES,
		    in.mtlb_physical, MMU_PAGE_BYTES) ||
		    mmu_physical_overlap(pages[i].physical, MMU_PAGE_BYTES,
		    in.stlb_physical, A733_NPU_MMU_STLB_BYTES))
			return EINVAL;
		for (size_t j = 0; j < i; j++) {
			if (pages[i].physical == pages[j].physical)
				return EINVAL;
		}
	}
	/* All input is now validated and private. No error follows publication. */
	for (size_t i = 0; i < A733_NPU_MMU_MTLB_BYTES / 4; i++)
		mmu_put32((uint8_t *)mtlb + i * 4, MMU_EXCEPTION);
	for (size_t i = 0; i < A733_NPU_MMU_STLB_BYTES / 4; i++)
		mmu_put32((uint8_t *)stlb + i * 4, MMU_EXCEPTION);
	mmu_put32((uint8_t *)mtlb + (in.virtual_base >> 24) * 4,
	    (uint32_t)in.stlb_physical | 1);
	for (size_t i = 0; i < in.page_count; i++) {
		uint32_t flags = (pages[i].flags & A733_NPU_MMU_WRITABLE) ? 7 : 3;
		mmu_put32((uint8_t *)stlb + (first + i) * 4,
		    (uint32_t)pages[i].physical | flags);
	}
	return 0;
}

int
sun60i_a733_npu_mmu_pd_entry(uint64_t physical, uint32_t *out)
{
	struct mmu_span span;

	if (!mmu_span(out, sizeof(*out), &span) ||
	    !mmu_physical(physical, MMU_PAGE_BYTES))
		return EINVAL;
	*out = (uint32_t)((physical >> 12) << 4) | 1;
	return 0;
}
