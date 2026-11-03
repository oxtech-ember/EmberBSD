/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD PowerVR DMA owner, AI-assisted. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Native CPU-owned PowerVR firmware memory. See a733-gpu-fwmem.md. */
#ifndef _SUN60I_A733_GPU_FWMEM_H
#define _SUN60I_A733_GPU_FWMEM_H

#include "sun60i_a733_gpu_fwimage.h"
#include "sun60i_a733_gpu_fwinit.h"
#include "sun60i_a733_gpu_fwstart.h"
#include "../../../dev/powervr/pvr_fwmem.h"

#define sun60i_a733_gpu_dma pvr_dma
#define sun60i_a733_gpu_fwmem pvr_fwmem

int sun60i_a733_gpu_dma_alloc_coherent(bus_dma_tag_t, size_t,
    struct sun60i_a733_gpu_dma *);
void sun60i_a733_gpu_dma_free(bus_dma_tag_t, struct sun60i_a733_gpu_dma *);


/*
 * Allocate, validate and prepare CPU-owned firmware memory. No device
 * registers or DMA descriptors receive its addresses. The caller serializes
 * this operation and keeps the input stable. All failures preserve *out.
 * Initial boot supports 36-bit DMA, single contiguous allocations up to
 * 256 KiB each, 4K remapped sections, one 4K stack, and a 16 KiB table.
 * Malformed overlapping DMA ranges return EEXIST. Unsupported layout
 * variants return EOPNOTSUPP; allocator and parser errors propagate.
 * This sleepable operation does not sync or transfer DMA ownership.
 * Destroy is legal only before a device receives any of these addresses.
 */
int sun60i_a733_gpu_fwmem_prepare(bus_dma_tag_t, const void *, size_t,
    uint64_t, struct sun60i_a733_gpu_fwmem **);
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
int sun60i_a733_gpu_fwmem_interface(struct sun60i_a733_gpu_fwmem *,
    uint64_t, uint32_t, uint32_t);

void sun60i_a733_gpu_fwmem_destroy(struct sun60i_a733_gpu_fwmem *);


#endif /* _SUN60I_A733_GPU_FWMEM_H */
