/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_MMU4_NATIVE_H_
#define _BCMV3D_MMU4_NATIVE_H_

#include <arm/broadcom/bcm2712_iommuc_native.h>
#include <arm/broadcom/bcm2712_mmu4_corevar.h>
#include "bcmv3d_mmu4_backing.h"

struct drm_gem_object;
struct bcmv3d_device;
struct bcmv3d_mmu4_native;

enum bcmv3d_mmu4_native_stage {
	BCMV3D_MMU4_NATIVE_CREATED,
	BCMV3D_MMU4_NATIVE_PREFIX,
	BCMV3D_MMU4_NATIVE_MAP,
	BCMV3D_MMU4_NATIVE_DEBUG,
	BCMV3D_MMU4_NATIVE_ALLOCATE,
	BCMV3D_MMU4_NATIVE_PREPARED,
	BCMV3D_MMU4_NATIVE_PROGRAM,
	BCMV3D_MMU4_NATIVE_COMPLETE
};

struct bcmv3d_mmu4_native_status {
	struct bcmv3d_mmu4_range_status range;
	enum bcmv3d_mmu4_native_stage stage;
	struct bcm2712_iommuc_native_status icac;
	struct bcm2712_iommuc_status shared;
	struct bcm2712_mmu4_status sequence; /* Snapshot BEFORE untouched fini. */
	struct bcmv3d_mmu4_backing_status backing;
	uint32_t debug;
	unsigned int reads_attempted, reads_completed;
	unsigned int writes_attempted, writes_completed, barriers;
	int prepare_error, program_error, operation_error, close_error;
	int status_error, fault;
	bool prepare_attempted, program_attempted, mapped, debug_valid;
	bool backing_created, sealed, write_attempted;
	bool icac_valid, shared_valid, sequence_valid, backing_valid;
};

/*
 * Private boot-lifetime display consumer. Process context only. The caller
 * supplies a live native BCMV3D device. Create pins its DRM reference and
 * validates canonical GPU0/ICAC identity; it does not map or access hardware.
 * Outputs use accessible disjoint storage and remain unchanged on error.
 *
 * BEFORE prepare, the caller must establish normal-active SoC fabric access,
 * real held clock lifetimes, console handoff and exclusion of all firmware,
 * MMU2/4/5/cache and local-MMU4 clients. Keep those conditions for every retained
 * object, including errors. No sample/API/framebuffer absence proves them.
 * Prepare explicitly observes ICAC once when pristine, then maps MMU4/reads
 * DEBUG and allocates actual backing. Program rechecks the cached prefix,
 * seals backing and invokes the fixed synchronous executor once. No allocation
 * occurs under its common mutex. Native bus stalls/aborts are not recoverable.
 *
 * Local mutex -> ICAC mutex. No re-entry or external use of the private owner
 * slot. Callbacks, mappings, backing and render/device/tag references remain
 * alive for the boot. There is no free, retry, disable, restore or detach API.
 * Completion is protocol only, not translation, handoff, fetch, drain or power.
 */
int bcmv3d_mmu4_native_create(struct bcmv3d_device *,
    struct bcmv3d_mmu4_native **);
int bcmv3d_mmu4_native_prepare(struct bcmv3d_mmu4_native *);
int bcmv3d_mmu4_native_program(struct bcmv3d_mmu4_native *);
int bcmv3d_mmu4_native_cached_status(struct bcmv3d_mmu4_native *,
    struct bcmv3d_mmu4_native_status *);

/*
 * Process context, with a caller-held live GEM reference. Serialize through
 * the existing native mutex. These calls do not enter ICAC, perform MMIO or
 * change initial backing/status. Slots 0..2 use IOVA 8/16/24 MiB, offset 0,
 * span 8294400. Staging is not content, fence or scanout readiness. Discard
 * releases only allocations and references that have never been published.
 */
int bcmv3d_mmu4_native_stage_gem(struct bcmv3d_mmu4_native *, unsigned int,
    struct drm_gem_object *);
int bcmv3d_mmu4_native_discard_staged_gem(struct bcmv3d_mmu4_native *,
    unsigned int);

/*
 * Translation only for the three never-HVS-latched private windows. Require
 * successful retained initial program. No initial one-shot guard is reset.
 * Publish/unmap can repeat after success; a runtime fault forbids later writes.
 * Unmap releases the actual BO/map outside ICAC; six linked L2 stay resident.
 * No HVS/KMS caller, producer fence or active-retirement contract is provided.
 */
int bcmv3d_mmu4_native_publish_staged_gem(struct bcmv3d_mmu4_native *,
    unsigned int);
int bcmv3d_mmu4_native_unmap_gem(struct bcmv3d_mmu4_native *, unsigned int);

#endif /* _BCMV3D_MMU4_NATIVE_H_ */
