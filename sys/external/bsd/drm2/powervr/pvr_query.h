/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _PVR_QUERY_H_
#define _PVR_QUERY_H_

#include <sys/types.h>
struct drm_pvr_ioctl_dev_query_args;

/* Values come from the parsed metadata of the prepared firmware image. */
enum pvr_query_feature {
	PVR_QUERY_COMMON_STORE,
	PVR_QUERY_MAX_PARTITIONS,
	PVR_QUERY_TILE_X,
	PVR_QUERY_TILE_Y,
	PVR_QUERY_MIN_OUTPUT_REGS,
	PVR_QUERY_NUM_CLUSTERS,
	PVR_QUERY_TILES_IN_FLIGHT,
	PVR_QUERY_ROGUEXE,
	PVR_QUERY_COMPUTE_OVERLAP,
	PVR_QUERY_FEATURE_COUNT
};

struct pvr_query_input {
	uint64_t bvnc, brn_mask, ern_mask;
	uint32_t fw_major, fw_minor, fw_build;
	uint64_t feature[PVR_QUERY_FEATURE_COUNT];
};

/* Native data. pvr_query.c owns UAPI serialization. */
struct pvr_query_runtime {
	uint64_t free_list_min_pages, free_list_max_pages;
	uint32_t common_store_alloc_region_size;
	uint32_t common_store_partition_space_size;
	uint32_t max_coeffs, cdm_max_local_mem_size_regs;
};

struct pvr_query_profile {
	struct pvr_query_runtime runtime;
	uint32_t num_phantoms;
	uint32_t quirks[5], enhancements[2];
	uint16_t quirk_count, musthave_count, enhancement_count;
};

/* Return native positive errno. Failure leaves the output unchanged. */
int pvr_query_profile_init(const struct pvr_query_input *,
    struct pvr_query_profile *);
/* The caller owns an immutable profile. Return Linux-style negative errno. */
int pvr_query(const struct pvr_query_profile *, uint32_t,
    struct drm_pvr_ioctl_dev_query_args *);

#endif /* _PVR_QUERY_H_ */
