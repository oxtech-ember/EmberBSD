/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD native V3D allocation diagnostics, 2026-10-16. */
#ifndef _BCMV3D_BO_STATS_H_
#define _BCMV3D_BO_STATS_H_

#include <sys/types.h>

#define BCMV3D_BO_STATS_VERSION 1

enum bcmv3d_bo_failure_stage {
	BCMV3D_BO_STAGE_NONE = 0,
	BCMV3D_BO_STAGE_REQUEST,
	BCMV3D_BO_STAGE_QUOTA,
	BCMV3D_BO_STAGE_VA,
	BCMV3D_BO_STAGE_DMA_ALLOC,
	BCMV3D_BO_STAGE_DMA_MAP,
	BCMV3D_BO_STAGE_MAP_CREATE,
	BCMV3D_BO_STAGE_MAP_LOAD,
	BCMV3D_BO_STAGE_GEOMETRY,
	BCMV3D_BO_STAGE_HANDLE,
	BCMV3D_BO_STAGE_COUNT
};

/* Native-endian sysctl snapshot. No pointers, physical addresses or MMIO. */
struct bcmv3d_bo_stats {
	uint32_t version;
	uint32_t length;
	uint64_t reserved_bytes;
	uint64_t highwater_bytes;
	uint64_t limit_bytes;
	uint64_t max_bo_bytes;
	uint64_t reservations;
	uint64_t releases;
	uint64_t failures;
	uint64_t failures_by_stage[BCMV3D_BO_STAGE_COUNT];
	uint64_t last_requested_bytes;
	uint64_t last_rounded_bytes;
	uint64_t last_reserved_bytes;
	uint32_t last_stage;
	uint32_t last_errno;
	uint64_t reserved[2];
};

#endif
