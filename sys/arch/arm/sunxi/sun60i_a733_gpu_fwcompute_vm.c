/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "sun60i_a733_gpu_fwcompute_vm.h"
#include "powervr/pvr_rogue_fwif.h"

_Static_assert(ROGUE_FW_BIF_INVALID_PCSET == UINT32_C(0xffffffff),
    "invalid PCSET value");

/* One distinct 2 MiB table window plus its enclosing 1 GiB directory. */
struct fwvm_window {
	uint64_t window_va;
	uint64_t pc_va;
	uint16_t pc, pd;
	uint32_t directory;
};

static void
fwvm_put32(uint8_t *out, uint32_t value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		out[i] = (uint8_t)(value >> (8 * i));
}

static void
fwvm_put64(uint8_t *out, uint64_t value)
{
	unsigned int i;

	for (i = 0; i < 8; i++)
		out[i] = (uint8_t)(value >> (8 * i));
}

static bool
fwvm_region_valid(const struct a733_gpu_fwcompute_vm_region *r)
{
	uint64_t pa_limit = (UINT64_C(1) << 36) - A733_GPU_FWCOMPUTE_VM_PAGE;
	uint64_t bytes = (uint64_t)r->pages * A733_GPU_FWCOMPUTE_VM_PAGE;

	if (r->pages == 0 || r->pages > 512 ||
	    (r->va & (A733_GPU_FWCOMPUTE_VM_PAGE - 1)) != 0 ||
	    (r->pa & (A733_GPU_FWCOMPUTE_VM_PAGE - 1)) != 0 ||
	    r->va > (UINT64_C(1) << 40) - bytes || r->pa > pa_limit ||
	    (r->flags & ~(A733_GPU_FWCOMPUTE_VM_RO |
	    A733_GPU_FWCOMPUTE_VM_BYPASS)) != 0 ||
	    (r->va & (A733_GPU_FWCOMPUTE_VM_WINDOW_VA - 1)) + bytes >
	    A733_GPU_FWCOMPUTE_VM_WINDOW_VA)
		return false;
	return true;
}

static uint32_t
fwvm_window_find(const struct fwvm_window *windows, uint32_t used,
    uint64_t window_va)
{
	uint32_t i;

	for (i = 0; i < used; i++) {
		if (windows[i].window_va == window_va)
			return i;
	}
	return used;
}

int
sun60i_a733_gpu_fwcompute_vm_encode(
    const struct a733_gpu_fwcompute_vm_region *regions, uint32_t count,
    void *page_catalog,
    void * const directory[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    void * const table[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t directory_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t table_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    uint64_t page_catalog_pa, uint8_t fw_context[32],
    struct a733_gpu_fwcompute_vm_summary *summary)
{
	const uint64_t window_mask = A733_GPU_FWCOMPUTE_VM_WINDOW_VA - 1;
	const uint64_t directory_mask =
	    (uint64_t)A733_GPU_FWCOMPUTE_VM_WINDOW_VA *
	    A733_GPU_MMU_PD_ENTRIES - 1;
	struct fwvm_window windows[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	uint32_t used = 0, directories = 0, leaves = 0;
	uint32_t i, p, j, q;

	if (regions == NULL || count == 0 || summary == NULL ||
	    fw_context == NULL || page_catalog == NULL || count > 16)
		return EINVAL;
	memset(windows, 0, sizeof(windows));
	memset(summary, 0, sizeof(*summary));
	for (i = 0; i < A733_GPU_FWCOMPUTE_VM_WINDOWS; i++) {
		if (directory[i] == NULL || table[i] == NULL)
			return EINVAL;
	}
	/* Pass one: validate shape, assign windows, reject all aliasing. */
	for (i = 0; i < count; i++) {
		uint64_t window_va, pc_va;
		uint32_t slot;

		if (!fwvm_region_valid(&regions[i]))
			return EINVAL;
		window_va = regions[i].va & ~window_mask;
		pc_va = regions[i].va & ~directory_mask;
		slot = fwvm_window_find(windows, used, window_va);
		if (slot == used) {
			if (used == A733_GPU_FWCOMPUTE_VM_WINDOWS)
				return EINVAL;
			for (j = 0; j < used; j++) {
				if (windows[j].pc_va == pc_va)
					break;
			}
			if (j < used) {
				slot = windows[j].directory;
			} else {
				if (directories ==
				    A733_GPU_FWCOMPUTE_VM_WINDOWS)
					return EINVAL;
				slot = directories++;
			}
			windows[used].window_va = window_va;
			windows[used].pc_va = pc_va;
			windows[used].pc = (uint16_t)(pc_va / (directory_mask + 1));
			windows[used].pd = (uint16_t)((regions[i].va >>
			    21) % A733_GPU_MMU_PD_ENTRIES);
			windows[used].directory = slot;
			used++;
		}
		/* No page may alias another page virtually or physically. */
		for (p = 0; p < regions[i].pages; p++) {
			uint64_t va = regions[i].va + (uint64_t)p *
			    A733_GPU_FWCOMPUTE_VM_PAGE;
			uint64_t pa = regions[i].pa + (uint64_t)p *
			    A733_GPU_FWCOMPUTE_VM_PAGE;

			for (j = 0; j < i; j++) {
				for (q = 0; q < regions[j].pages; q++) {
					if (regions[j].va + (uint64_t)q *
					    A733_GPU_FWCOMPUTE_VM_PAGE == va)
						return EINVAL;
					if (regions[j].pa + (uint64_t)q *
					    A733_GPU_FWCOMPUTE_VM_PAGE == pa)
						return EINVAL;
				}
			}
			for (q = 0; q < p; q++) {
				if (regions[i].va + (uint64_t)q *
				    A733_GPU_FWCOMPUTE_VM_PAGE == va ||
				    regions[i].pa + (uint64_t)q *
				    A733_GPU_FWCOMPUTE_VM_PAGE == pa)
					return EINVAL;
			}
		}
		leaves += regions[i].pages;
	}
	/* Pass two: encode every table and the firmware memory context. */
	for (i = 0; i < count; i++) {
		uint32_t slot = fwvm_window_find(windows, used,
		    regions[i].va & ~window_mask);
		const struct fwvm_window *w = &windows[slot];
		uint64_t pde;
		uint32_t pce;
		int error;

		for (p = 0; p < regions[i].pages; p++) {
			struct sun60i_a733_gpu_mmu_indices indices;
			uint64_t pte;

			error = sun60i_a733_gpu_mmu_indices(regions[i].va +
			    (uint64_t)p * A733_GPU_FWCOMPUTE_VM_PAGE, &indices);
			if (error != 0)
				return error;
			error = sun60i_a733_gpu_mmu_pte(regions[i].pa +
			    (uint64_t)p * A733_GPU_FWCOMPUTE_VM_PAGE,
			    regions[i].flags, &pte);
			if (error != 0)
				return error;
			if (indices.pc != w->pc || indices.pd != w->pd)
				return EINVAL;
			fwvm_put64((uint8_t *)table[slot] + indices.pt * 8,
			    pte);
		}
		error = sun60i_a733_gpu_mmu_pde(table_pa[slot], &pde);
		if (error != 0)
			return error;
		fwvm_put64((uint8_t *)directory[w->directory] + w->pd * 8,
		    pde);
		error = sun60i_a733_gpu_mmu_pce(directory_pa[w->directory],
		    &pce);
		if (error != 0)
			return error;
		fwvm_put32((uint8_t *)page_catalog + w->pc * 4, pce);
		summary->window[slot].window_va = w->window_va;
		summary->window[slot].pc_va = w->pc_va;
		summary->window[slot].pc = w->pc;
		summary->window[slot].pd = w->pd;
		summary->window[slot].leaves += regions[i].pages;
	}
	summary->windows = used;
	summary->directories = directories;
	summary->leaves = leaves;
	fwvm_put64(fw_context, page_catalog_pa);
	fwvm_put32(fw_context + 8, ROGUE_FW_BIF_INVALID_PCSET);
	return 0;
}
