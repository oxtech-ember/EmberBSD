/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD PowerVR DMA owner, AI-assisted. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Native CPU-owned PowerVR firmware memory. See a733-gpu-fwmem.md. */
#ifndef _PVR_FWMEM_H
#define _PVR_FWMEM_H

#include <sys/types.h>
#include <sys/bus.h>
#include "pvr_fwimage.h"
#include "pvr_fwinit.h"

struct pvr_fwmem;

struct pvr_dma {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	size_t size;
	bus_addr_t address;
	int nsegments;
	bool loaded;
	/* Requested and verified coherent mapping, with no bounce buffer. */
	bool coherent;
	/* Compute allocation cannot be reclaimed after device exposure. */
	bool device_exposed;
	struct pvr_fwmem *compute_owner;
};


struct pvr_fwmem {
	bus_dma_tag_t dmat;
	struct pvr_dma code, data, table, work, config;
	struct pvr_fwimage image;
	bus_addr_t boot_code, boot_data, exception_code, stack;
	struct pvr_fwinit_layout interface_layout;
	bool interface_prepared;
	/* Retained after any possible device exposure; owner serializes access. */
	bool device_exposed;
	enum pvr_fw_profile profile;
	uint64_t register_pa, register_size, auxiliary_pa, auxiliary_size;
};

/*
 * Allocate, validate and prepare CPU-owned firmware memory. No device
 * registers or DMA descriptors receive its addresses. The caller serializes
 * this operation and keeps the input stable. All failures preserve *out.
 * Initial boot supports 36-bit DMA, single contiguous allocations up to
 * 256 KiB each, 4K remapped sections, one 4K stack, and a 16 KiB table.
 * Malformed overlapping DMA ranges return EEXIST. Unsupported layout
 * variants return EOPNOTSUPP; allocator and parser errors propagate.
 * This sleepable operation does not sync or transfer DMA ownership.
 * Supply both platform-validated MMIO resources; no DMA allocation may
 * overlap them. Before allocation the platform must establish that the
 * native VM allocator excludes all reserved RAM. GE requires direct,
 * non-bouncing DMA.
 * Destroy returns EBUSY and retains every allocation after possible exposure.
 */
int pvr_fwmem_prepare(bus_dma_tag_t, const void *, size_t, uint64_t,
    enum pvr_fw_profile, uint64_t, uint64_t, uint64_t, uint64_t,
    struct pvr_fwmem **);
/*
 * Add FWIF work/config arenas to a successfully prepared, CPU-owned object.
 * The caller serializes access and supplies verified hardware values.
 * This sleepable operation publishes no device address and performs no DMA
 * sync. Use a heap shadow of the page table until every initialization and
 * mapping succeeds. On failure, keep all existing buffers and fields intact.
 * Retry after failure is allowed. A successful call is single-use: EBUSY.
 * Work/config require BUS_DMA_COHERENT at CPU mapping time. Their loaded
 * maps must report _BUS_DMAMAP_COHERENT and no _BUS_DMAMAP_IS_BOUNCING.
 * Missing coherency or a bounce returns EOPNOTSUPP before publication.
 * No live mapping is replaced. Code/data/table keep their original mapping.
 * Return EINVAL for an incomplete owner; propagate allocator/helper errors.
 */
int pvr_fwmem_interface(struct pvr_fwmem *,
    uint64_t, uint32_t, uint32_t);

/* Allocate direct coherent GE compute RAM under an already exposed FW owner.
 * Memory is uninitialized: the caller checks overlap with its other children
 * before writing. The caller owns the descriptor, marks it exposed before any device
 * publication, and must retain it after an uncertain operation. Free refuses
 * an exposed descriptor; neither function changes the firmware owner. */
int pvr_fwmem_compute_alloc(struct pvr_fwmem *, size_t, struct pvr_dma *);
int pvr_fwmem_compute_free(struct pvr_fwmem *, struct pvr_dma *);

/* NULL is a no-op; an exposed owner returns EBUSY without releasing RAM. */
int pvr_fwmem_destroy(struct pvr_fwmem *);

#endif
