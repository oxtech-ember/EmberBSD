/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_MMU4_BACKING_H_
#define _BCMV3D_MMU4_BACKING_H_

#include <sys/types.h>

struct drm_gem_object;
struct bcmv3d_scanout_frame;
struct bcmv3d_device;
struct bcmv3d_mmu4_backing;
struct bcm2712_mmu4_config;

enum bcmv3d_mmu4_backing_stage {
	BCMV3D_MMU4_BACKING_IDLE,
	BCMV3D_MMU4_BACKING_VALIDATE,
	BCMV3D_MMU4_BACKING_ENCODE,
	BCMV3D_MMU4_BACKING_ZERO,
	BCMV3D_MMU4_BACKING_COPY,
	BCMV3D_MMU4_BACKING_SYNC,
	BCMV3D_MMU4_BACKING_PREPARED
};

enum bcmv3d_mmu4_range_operation {
	BCMV3D_MMU4_RANGE_PUBLISH,
	BCMV3D_MMU4_RANGE_UNMAP
};

/* Actual last-operation diagnostics. These fields grant no access. */
struct bcmv3d_mmu4_range_status {
	enum bcmv3d_mmu4_range_operation operation;
	unsigned int slot, stores, syncs, polls, barriers, reads, writes;
	uint32_t raw_control;
	int first_error;
	bool attempted, mutation_attempted, complete;
};

struct bcmv3d_mmu4_backing_status {
	enum bcmv3d_mmu4_backing_stage stage;
	uint64_t frame_bytes, control_bytes;
	unsigned int fragments, pmap_calls, syncs;
	int first_error;
	bool prepare_started, prepared, sealed, mapping_uncertain;
};

/*
 * Private fixed 1920x1080 XRGB8888 backing. Prepare fills an immutable
 * eight-bar initial test pattern before PREWRITE. Process context only.
 * The caller holds the native render/device/tag references, serializes
 * all access and preserves callback storage/text. No object may be exposed to
 * a user, job, device or another writer. Supply accessible live objects and
 * disjoint output storage. Constructor/get-config/status preserve output on
 * error and return positive BSD errno. No MMIO or shared lease operation
 * occurs.
 *
 * The exact prepare callback uses preallocated heap plan/images, the actual
 * encoder and full PREWRITE. It is one-shot, including failure. Sync has no
 * recoverable native error result; a returned callback does not prove DMA,
 * translation, power, exclusion or drain.
 *
 * Seal before entering a future sequencer program, including its preflight.
 * Seal is irreversible. Destroy refuses a sealed/running object or uncertain
 * mapping. This policy
 * retains even a pre-retain refusal. It provides no release-after-publication
 * operation. A failed repeated native mapping proof latches uncertainty and
 * retains backing even without seal. A future MMIO adapter must refuse
 * unsealed backing itself.
 */
int bcmv3d_mmu4_backing_create(struct bcmv3d_device *, uint32_t,
    struct bcmv3d_mmu4_backing **);
int bcmv3d_mmu4_backing_get_config(const struct bcmv3d_mmu4_backing *,
    struct bcm2712_mmu4_config *);
int bcmv3d_mmu4_backing_prepare(void *, const struct bcm2712_mmu4_config *);
int bcmv3d_mmu4_backing_status(const struct bcmv3d_mmu4_backing *,
    struct bcmv3d_mmu4_backing_status *);
int bcmv3d_mmu4_backing_seal(struct bcmv3d_mmu4_backing *);
int bcmv3d_mmu4_backing_destroy_unexposed(struct bcmv3d_mmu4_backing *);

/*
 * Private unpublished GEM staging in three fixed IOVA windows. The caller
 * holds a live GEM reference and serializes access. No content/fence readiness,
 * DMA sync, active table change or hardware publication is implied. Initial
 * backing must be prepared. Seal does not prevent this separate staging.
 * Discard frees only never-published slot resources, not retired scanout.
 */
int bcmv3d_mmu4_backing_stage_gem(struct bcmv3d_mmu4_backing *, unsigned int,
    struct drm_gem_object *);
int bcmv3d_mmu4_backing_discard_staged_gem(struct bcmv3d_mmu4_backing *,
    unsigned int);

/*
 * Trusted native retained-executor callback only, serialized by native mutex.
 * invalidate completes shared and local invalidation synchronously. No memory
 * allocation/free is allowed here. Outputs are actual operation diagnostics.
 * Slots have never been passed to HVS: the only current HVS API uses IOVA0.
 * Active scanout retirement is unsupported. A linked L2 pair stays resident.
 */
int bcmv3d_mmu4_backing_range_check(struct bcmv3d_mmu4_backing *,
    unsigned int, enum bcmv3d_mmu4_range_operation);
int bcmv3d_mmu4_backing_range(struct bcmv3d_mmu4_backing *, unsigned int,
    enum bcmv3d_mmu4_range_operation, int (*)(void *), void *,
    struct bcmv3d_mmu4_range_status *);
/* Propagate a post-exposure native/provider error without freeing resources. */
void bcmv3d_mmu4_backing_range_abort(struct bcmv3d_mmu4_backing *, int);
/* After successful UNMAP, outside ICAC mutex. Does not free resident L2. */
int bcmv3d_mmu4_backing_reclaim_unmapped(struct bcmv3d_mmu4_backing *,
    unsigned int);

/*
 * Caller-owned reader lifetime. Before prepare, wait for the prior writer and
 * publish/retain a bcmv3d_read lease outside the native mutex. Keep it alive and
 * prohibit CPU writes until cancellation or actual hardware retirement.
 * Prepare validates the exact staged BO and syncs its actual consumer DMA map.
 * Discard/unmap pair POSTWRITE before unload/ref release only while unexposed.
 * They do not signal or drop the caller's read lease. End that lease after map
 * cleanup. These methods do not wait, allocate, publish HVS or acquire fences.
 *
 * After successful translation publication, call expose BEFORE the first HVS
 * write that could latch this range, including a write that might fail.
 * Exposure is irreversible here: discard, unmap and reclaim refuse it. Keep
 * the caller's lease, BO, map and hardware resources alive on every outcome.
 * A new retirement implementation must prove the actual consumer is finished;
 * a programmed list pointer, timeout or disabled-channel bit is insufficient.
 */
int bcmv3d_mmu4_backing_prepare_gem_read(struct bcmv3d_mmu4_backing *,
    unsigned int, struct drm_gem_object *);
int bcmv3d_mmu4_backing_expose_gem(struct bcmv3d_mmu4_backing *,
    unsigned int, struct drm_gem_object *);

/* Internal native worker authorization; no caller-provided completion bool. */
int bcmv3d_mmu4_backing_authorize_retired(struct bcmv3d_mmu4_backing *,
    unsigned int, struct bcmv3d_scanout_frame *);

#endif /* _BCMV3D_MMU4_BACKING_H_ */
