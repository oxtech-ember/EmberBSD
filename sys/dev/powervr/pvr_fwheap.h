/* Origin: EmberBSD; A733-profile firmware heap mechanism. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Firmware-heap placement and compute VM backing. See a733-gpu-fwheap.md. */
#ifndef _PVR_FWHEAP_H
#define _PVR_FWHEAP_H

#include <sys/types.h>
#include <sys/bus.h>
#include "pvr_fwmem.h"
#include "pvr_fwcompute.h"
#include "pvr_fwcompute_file.h"
#include "pvr_fwcompute_vm.h"

#define PVR_FWHEAP_PAGES	9
#define PVR_FWHEAP_BYTES	(PVR_FWHEAP_PAGES * 4096)

/* Offsets of the six serialized objects inside the heap buffer. */
#define PVR_FWHEAP_CONTEXT_OFF	0
#define PVR_FWHEAP_CTL_OFF		232
#define PVR_FWHEAP_CCCB_OFF	264
#define PVR_FWHEAP_STATE_OFF	33032
#define PVR_FWHEAP_TIMELINE_OFF	33040
#define PVR_FWHEAP_VM_CONTEXT_OFF	33048

/* A733-profile allocator; not a GE DMA ownership interface. */
int pvr_heap_dma_alloc_coherent(bus_dma_tag_t, size_t, struct pvr_dma *);
void pvr_heap_dma_free(bus_dma_tag_t, struct pvr_dma *);

struct pvr_fwheap {
	bus_dma_tag_t dmat;
	struct pvr_fwmem *owner;
	struct pvr_dma heap;
	uint32_t fw_addr;
	enum pvr_fw_profile profile;
	bool prepared, mapped, sync_attempted, sync_complete, kick_attempted;
};

struct pvr_fwvm {
	bus_dma_tag_t dmat;
	struct pvr_fwheap *heap;
	bool device_exposed;
	struct pvr_dma resource[PVR_FWCOMPUTE_RESOURCES];
	struct pvr_dma catalog, directory[5], table[5];
	uint64_t catalog_address;
	uint32_t windows, directories, leaves;
	bool prepared;
};

/*
 * Allocate the coherent nine-page heap, copy the six serialized objects
 * at their exact firmware offsets, and leave the VM-context slot zero.
 * No table edit or publication happens here.
 */
int pvr_fwheap_prepare(bus_dma_tag_t,
    struct pvr_fwmem *,
    const struct pvr_fwcompute_image *,
    struct pvr_fwheap **);

/*
 * Map the heap pages into the live firmware wrapper table at the declared
 * window base. Existing entries are never replaced. The caller must send
 * an MMUCACHE sync afterwards so the firmware observes the new mappings.
 */
int pvr_fwheap_map(struct pvr_fwheap *,
    struct pvr_fwmem *);

/* Write the 32-byte firmware memory context into the mapped heap slot. */
int pvr_fwheap_set_vm_context(struct pvr_fwheap *,
    const uint8_t[32]);

/* Sample the client-queue control and the timeline word. */
void pvr_fwheap_observe(const struct pvr_fwheap *,
    uint32_t *, uint32_t *, uint32_t *);

/* GE release preserves the pointer and all DMA after possible exposure. */
int pvr_fwheap_release(struct pvr_fwheap **);
void pvr_fwheap_destroy(struct pvr_fwheap **);

/*
 * Back every file resource with its own coherent pages, copy the compiler
 * bytes, build the catalog/directory/table pages through the portable
 * encoder, and place the resulting firmware memory context into the heap.
 * The caller supplies a validated, stable file and bytes. For GE, the VM
 * allocations are marked exposed before the live context is written. An
 * error after that point returns a retained owner in *out; release returns
 * EBUSY. DMA syncs make the prepared tables visible to the device.
 */
int pvr_fwvm_prepare(bus_dma_tag_t,
    const struct pvr_fwcompute_file *, const uint8_t *,
    struct pvr_fwheap *, struct pvr_fwvm **);

int pvr_fwvm_release(struct pvr_fwvm **);
void pvr_fwvm_destroy(struct pvr_fwvm **);

#endif /* _PVR_FWHEAP_H */
