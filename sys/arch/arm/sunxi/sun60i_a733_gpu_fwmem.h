/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Native CPU-owned PowerVR firmware memory. See a733-gpu-fwmem.md. */
#ifndef _SUN60I_A733_GPU_FWMEM_H
#define _SUN60I_A733_GPU_FWMEM_H

#include <sys/types.h>
#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_gpu_fwimage.h>

struct sun60i_a733_gpu_dma {
	bus_dma_segment_t segment;
	bus_dmamap_t map;
	void *cpu;
	size_t size;
	bus_addr_t address;
	int nsegments;
	bool loaded;
};

struct sun60i_a733_gpu_fwmem {
	bus_dma_tag_t dmat;
	struct sun60i_a733_gpu_dma code, data, table;
	struct sun60i_a733_gpu_fwimage image;
	bus_addr_t boot_code, boot_data, exception_code, stack;
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
 * Destroy is legal only before a device receives any of these addresses.
 */
int sun60i_a733_gpu_fwmem_prepare(bus_dma_tag_t, const void *, size_t,
    uint64_t, struct sun60i_a733_gpu_fwmem **);
void sun60i_a733_gpu_fwmem_destroy(struct sun60i_a733_gpu_fwmem *);

#endif
