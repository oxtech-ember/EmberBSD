/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* MIT adaptation provenance and permission notice: pvr_freelist.c. */
#ifndef _PVR_FREELIST_H_
#define _PVR_FREELIST_H_

#include <sys/types.h>
#include <sys/bus.h>

struct pvr_backend;
struct pvr_freelist;

struct pvr_freelist_config {
	void *backing_cpu;
	size_t backing_size;
	uint64_t backing_pa, gpu_va;
	void *sync_cookie;
	void (*backing_sync)(void *, int);
	uint32_t id, initial_pages, max_pages, grow_pages, grow_threshold;
};

/*
 * Positive native errno. The caller holds the device and backend gates.
 * It retains the VM and its GEM mapping until destroy clears the owner slot.
 * The backing span must be PM_FW_PROTECT without CPU userspace access.
 * The sync callback covers that retained GEM object's real DMA mapping.
 * The caller supplies an exclusive table span and a live firmware ID.
 * No HWRT or job may reference the object when destroy begins.
 *
 * The output slot starts NULL. A non-NULL output on error is still owned.
 * Failed cleanup or firmware unmapping retains all resources still owned.
 * The caller must retain that state until a proven reset if retry cannot run.
 * A never-exposed, uninitialized firmware object does not require CLEANUP.
 * This slice creates the initial PM pages and the ready-page reserve only.
 * FWCCB growth/reconstruction is not implemented. The transport must report
 * those requests as unsupported and stop execution; it must not fake success.
 */
int pvr_freelist_create(bus_dma_tag_t, const struct pvr_backend *,
    const struct pvr_freelist_config *, struct pvr_freelist **);
int pvr_freelist_destroy(struct pvr_freelist **);
/* Kernel-only firmware address. Zero means the object is not available. */
uint32_t pvr_freelist_fw_address(const struct pvr_freelist *);

#endif /* _PVR_FREELIST_H_ */
