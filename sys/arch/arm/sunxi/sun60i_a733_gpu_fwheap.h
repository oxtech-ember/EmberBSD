/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Firmware-heap placement and compute VM backing. See a733-gpu-fwheap.md. */
#ifndef _SUN60I_A733_GPU_FWHEAP_H
#define _SUN60I_A733_GPU_FWHEAP_H

#include <sys/types.h>
#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_gpu_fwmem.h>
#include <arm/sunxi/sun60i_a733_gpu_fwcompute.h>
#include <arm/sunxi/sun60i_a733_gpu_fwcompute_file.h>

#define A733_GPU_FWHEAP_PAGES	9
#define A733_GPU_FWHEAP_BYTES	(A733_GPU_FWHEAP_PAGES * 4096)

/* Offsets of the six serialized objects inside the heap buffer. */
#define A733_GPU_FWHEAP_CONTEXT_OFF	0
#define A733_GPU_FWHEAP_CTL_OFF		232
#define A733_GPU_FWHEAP_CCCB_OFF	264
#define A733_GPU_FWHEAP_STATE_OFF	33032
#define A733_GPU_FWHEAP_TIMELINE_OFF	33040
#define A733_GPU_FWHEAP_VM_CONTEXT_OFF	33048

struct sun60i_a733_gpu_fwheap {
	bus_dma_tag_t dmat;
	struct sun60i_a733_gpu_dma heap;
	uint32_t fw_addr;
	bool prepared, mapped;
};

struct sun60i_a733_gpu_fwvm {
	bus_dma_tag_t dmat;
	struct sun60i_a733_gpu_dma resource[9];
	struct sun60i_a733_gpu_dma catalog, directory[3], table[3];
	uint64_t catalog_address;
	uint32_t windows, directories, leaves;
	bool prepared;
};

/*
 * Allocate the coherent nine-page heap, copy the six serialized objects
 * at their exact firmware offsets, and leave the VM-context slot zero.
 * No table edit or publication happens here.
 */
int sun60i_a733_gpu_fwheap_prepare(bus_dma_tag_t,
    struct sun60i_a733_gpu_fwmem *,
    const struct sun60i_a733_gpu_fwcompute_image *,
    struct sun60i_a733_gpu_fwheap **);

/*
 * Map the heap pages into the live firmware wrapper table at the declared
 * window base. Existing entries are never replaced. The caller must send
 * an MMUCACHE sync afterwards so the firmware observes the new mappings.
 */
int sun60i_a733_gpu_fwheap_map(struct sun60i_a733_gpu_fwheap *,
    struct sun60i_a733_gpu_fwmem *);

/* Write the 32-byte firmware memory context into the mapped heap slot. */
int sun60i_a733_gpu_fwheap_set_vm_context(struct sun60i_a733_gpu_fwheap *,
    const uint8_t[32]);

/* Sample the client-queue control and the timeline word. */
void sun60i_a733_gpu_fwheap_observe(const struct sun60i_a733_gpu_fwheap *,
    uint32_t *, uint32_t *, uint32_t *);

void sun60i_a733_gpu_fwheap_destroy(struct sun60i_a733_gpu_fwheap **);

/*
 * Back every file resource with its own coherent pages, copy the compiler
 * bytes, build the catalog/directory/table pages through the portable
 * encoder, and place the resulting firmware memory context into the heap.
 * No publication or cache operation happens here.
 */
int sun60i_a733_gpu_fwvm_prepare(bus_dma_tag_t,
    const struct a733_gpu_fwcompute_file *, const uint8_t *,
    struct sun60i_a733_gpu_fwheap *, struct sun60i_a733_gpu_fwvm **);

void sun60i_a733_gpu_fwvm_destroy(struct sun60i_a733_gpu_fwvm **);

#endif /* _SUN60I_A733_GPU_FWHEAP_H */
