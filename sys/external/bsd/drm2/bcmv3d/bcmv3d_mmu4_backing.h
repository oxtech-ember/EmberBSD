/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_MMU4_BACKING_H_
#define _BCMV3D_MMU4_BACKING_H_

#include <sys/types.h>

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

struct bcmv3d_mmu4_backing_status {
	enum bcmv3d_mmu4_backing_stage stage;
	uint64_t frame_bytes, control_bytes;
	unsigned int fragments, pmap_calls, syncs;
	int first_error;
	bool prepare_started, prepared, sealed, mapping_uncertain;
};

/*
 * Private fixed, initially black 1920x1080 XRGB8888 backing. Process context
 * only. The caller holds the native render/device/tag references, serializes
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

#endif /* _BCMV3D_MMU4_BACKING_H_ */
