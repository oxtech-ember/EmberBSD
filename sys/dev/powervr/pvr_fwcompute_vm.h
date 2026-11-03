/* Origin: EmberBSD; common PowerVR representation. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* GPU VM tables for compute submission. See a733-gpu-fwcompute-vm.md. */
#ifndef _PVR_FWCOMPUTE_VM_H
#define _PVR_FWCOMPUTE_VM_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#endif

#include "pvr_mmu.h"

/* Bound the number of distinct page-table windows. */
#define PVR_FWCOMPUTE_VM_WINDOWS	5
#define PVR_FWCOMPUTE_VM_PAGE	4096
#define PVR_FWCOMPUTE_VM_WINDOW_VA	(2 * 1024 * 1024)

/* Region PTE options, mirroring the manifest access and cache policy. */
#define PVR_FWCOMPUTE_VM_RO	PVR_MMU_PTE_READ_ONLY
#define PVR_FWCOMPUTE_VM_BYPASS	PVR_MMU_PTE_SLC_BYPASS

/* One contiguous VA range over contiguous physical pages, one policy. */
struct pvr_fwcompute_vm_region {
	uint64_t va;
	uint64_t pa;
	uint32_t pages;
	uint64_t flags;	/* only RO and BYPASS; zero means read-write cached */
};

struct pvr_fwcompute_vm_slot {
	uint64_t window_va;	/* 2 MiB-aligned base of this window */
	uint64_t pc_va;		/* 1 GiB-aligned base of its directory */
	uint16_t pc, pd;
	uint32_t leaves;
};

struct pvr_fwcompute_vm_summary {
	struct pvr_fwcompute_vm_slot
	    window[PVR_FWCOMPUTE_VM_WINDOWS];
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
 * physical space, and must not overlap in VA. Exact whole-backing physical aliases are
 * permitted only with identical flags; partial aliases are rejected. Table
 * and catalog pages must be distinct and not overlap resource pages. At most WINDOWS
 * distinct windows and directories are supported. Writes are idempotent.
 * On failure, fw_context stays unchanged. Discard all other output
 * buffers and the summary; table encoding can fail after partial writes.
 * The return is a positive errno. This encodes representation only: no
 * allocation, sync, publication, or device acceptance is implied.
 */
int pvr_fwcompute_vm_encode(
    const struct pvr_fwcompute_vm_region *, uint32_t,
    void *page_catalog,
    void * const directory[PVR_FWCOMPUTE_VM_WINDOWS],
    void * const table[PVR_FWCOMPUTE_VM_WINDOWS],
    const uint64_t directory_pa[PVR_FWCOMPUTE_VM_WINDOWS],
    const uint64_t table_pa[PVR_FWCOMPUTE_VM_WINDOWS],
    uint64_t page_catalog_pa, uint8_t fw_context[32],
    struct pvr_fwcompute_vm_summary *);

#endif /* _PVR_FWCOMPUTE_VM_H */
