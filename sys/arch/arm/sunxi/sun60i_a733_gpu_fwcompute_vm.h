/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* GPU VM tables for the compute submission. See a733-gpu-fwheap.md. */
#ifndef _SUN60I_A733_GPU_FWCOMPUTE_VM_H
#define _SUN60I_A733_GPU_FWCOMPUTE_VM_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

#include "sun60i_a733_gpu_mmu.h"

/* Initial compute scope: the pinned manifest uses three distinct windows. */
#define A733_GPU_FWCOMPUTE_VM_WINDOWS	4
#define A733_GPU_FWCOMPUTE_VM_PAGE	4096
#define A733_GPU_FWCOMPUTE_VM_WINDOW_VA	(2 * 1024 * 1024)

/* Region PTE options, mirroring the manifest access and cache policy. */
#define A733_GPU_FWCOMPUTE_VM_RO	A733_GPU_MMU_PTE_READ_ONLY
#define A733_GPU_FWCOMPUTE_VM_BYPASS	A733_GPU_MMU_PTE_SLC_BYPASS

/* One contiguous VA range over contiguous physical pages, one policy. */
struct a733_gpu_fwcompute_vm_region {
	uint64_t va;
	uint64_t pa;
	uint32_t pages;
	uint64_t flags;	/* only RO and BYPASS; zero means read-write cached */
};

struct a733_gpu_fwcompute_vm_slot {
	uint64_t window_va;	/* 2 MiB-aligned base of this window */
	uint64_t pc_va;		/* 1 GiB-aligned base of its directory */
	uint16_t pc, pd;
	uint32_t leaves;
};

struct a733_gpu_fwcompute_vm_summary {
	struct a733_gpu_fwcompute_vm_slot
	    window[A733_GPU_FWCOMPUTE_VM_WINDOWS];
	uint32_t windows;	/* used 2 MiB tables */
	uint32_t directories;	/* used 1 GiB directories */
	uint32_t leaves;
};

/*
 * Encode the page catalog, one directory per used 1 GiB region, one table
 * per used 2 MiB window, and the 32-byte little-endian
 * rogue_fwif_fwmemcontext. Every page buffer is a distinct caller-owned
 * 4096-byte span, zeroed by the caller. Table physical addresses follow
 * the buffer array order. Regions must be four-kilobyte aligned, nonempty,
 * inside the 40-bit space, fit one 2 MiB window each, stay within 36-bit
 * physical space, and must not overlap in VA or PA. At most WINDOWS
 * distinct windows and directories are supported. Writes are idempotent;
 * on any validation error every output byte stays unchanged.
 * The return is a positive errno. This encodes representation only: no
 * allocation, sync, publication, or device acceptance is implied.
 */
int sun60i_a733_gpu_fwcompute_vm_encode(
    const struct a733_gpu_fwcompute_vm_region *, uint32_t,
    void *page_catalog,
    void * const directory[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    void * const table[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t directory_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    const uint64_t table_pa[A733_GPU_FWCOMPUTE_VM_WINDOWS],
    uint64_t page_catalog_pa, uint8_t fw_context[32],
    struct a733_gpu_fwcompute_vm_summary *);

#endif /* _SUN60I_A733_GPU_FWCOMPUTE_VM_H */
