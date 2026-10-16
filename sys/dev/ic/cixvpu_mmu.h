/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: original native implementation, AI-assisted. */
#ifndef _DEV_IC_CIXVPU_MMU_H_
#define _DEV_IC_CIXVPU_MMU_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define CIXVPU_MMU_PAGE_BYTES 4096U
#define CIXVPU_MMU_ENTRIES 1024U

enum cixvpu_mmu_attribute {
	CIXVPU_MMU_PRIVATE = 0,
	CIXVPU_MMU_COHERENT = 1,
	CIXVPU_MMU_SHARED_RO = 2,
	CIXVPU_MMU_SHARED_RW = 3
};

enum cixvpu_mmu_permission {
	CIXVPU_MMU_READ = 1,
	CIXVPU_MMU_EXECUTE = 2,
	CIXVPU_MMU_WRITE = 3
};

/*
 * These functions edit stopped-device page tables only. The caller owns
 * allocation, locking, bus_dma synchronization, ordering and TLB invalidation.
 * Addresses are DMA bus addresses, never CPU pointers or inferred physical
 * addresses. An external IOMMU mapping, when present, belongs to bus_dma.
 * Each supplied table must contain exactly one 4 KiB page of little-endian
 * entries. A firmware address selects L1 bits 31:22 and L2 bits 21:12.
 * This module neither starts DMA nor makes table removal safe during DMA.
 */
int cixvpu_mmu_encode(uint64_t, unsigned int, unsigned int, uint32_t *);
int cixvpu_mmu_link(void *, size_t, uint32_t, uint64_t);
int cixvpu_mmu_map(void *, size_t, uint32_t, uint64_t, unsigned int,
    unsigned int);
/* Unlink using the exact previously installed entry, after hardware quiescence. */
int cixvpu_mmu_remove(void *, size_t, unsigned int, uint32_t);
int cixvpu_mmu_lookup(const void *, size_t, unsigned int, uint32_t *);

#endif
