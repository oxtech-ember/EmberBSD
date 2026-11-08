/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* MIT adaptation provenance and permission notice: pvr_hwrt.c. */
#ifndef _PVR_HWRT_H_
#define _PVR_HWRT_H_

#include <sys/types.h>

struct pvr_backend;
struct pvr_hwrt;

struct pvr_hwrt_config {
	uint64_t bvnc;
	uint64_t tpc_dev_addr, vheap_table_dev_addr, rtc_dev_addr;
	struct {
		uint64_t pm_mlist_dev_addr;
		uint64_t macrotile_array_dev_addr;
		uint64_t region_header_dev_addr;
	} rt[2];
	uint32_t freelist_fw_addr[2];
	uint32_t width, height, samples, layers;
	uint32_t tpc_size, tpc_stride, region_header_size;
	uint32_t isp_merge_lower_x, isp_merge_lower_y;
	uint32_t isp_merge_upper_x, isp_merge_upper_y;
	uint32_t isp_merge_scale_x, isp_merge_scale_y;
	uint32_t local_pages, min_local_pages;
};

struct pvr_hwrt_target {
	uint32_t fw_address;
	uint32_t cleanup_address;
};

/*
 * Positive native errno. The caller holds the device and backend gates.
 * The caller retains both free lists, their VM and all GPU mapping spans.
 * It checks that both free lists and each render context use that same VM.
 * GPU addresses, spans and BO flags are checked before this module is called.
 * Two profiles use 16x16 tiles. BVNC 36.56.104.183 uses parameter format 2
 * and renders up to 8192x8192. BVNC 22.102.54.38 (GE8300) uses parameter
 * format 1 with one macrotile and no macrotile array, up to 4096x4096.
 * Samples are 1, 2 or 4 and layers are 1..2048; GE8300 accepts only one
 * sample and one layer.
 * region_header_size counts headers; the caller separately computes the
 * aligned byte span for the BO hold. tpc_size counts bytes, and tpc_stride
 * counts 4096-byte PM pages. GE8300 requires a zero macrotile array address;
 * BVNC 36.56.104.183 accepts zero or a valid array.
 *
 * The output slot starts NULL. A non-NULL result on error is still owned.
 * target marks that RT exposed before the caller can publish its first KICK.
 * Each KICK uses cleanup_address; a paired partial render uses the geometry
 * KICK's cleanup control only. Queue policy remains with the caller.
 * No jobs may retain the dataset when destroy begins. A failed destroy keeps
 * the owner and all dependencies live. Retry a busy cleanup; retain state
 * until proven reset when a transport or unmapping failure prevents retry.
 * Never-exposed RT objects require no firmware cleanup acknowledgement.
 */
int pvr_hwrt_create(const struct pvr_backend *, const struct pvr_hwrt_config *,
    struct pvr_hwrt **);
int pvr_hwrt_target(struct pvr_hwrt *, unsigned, struct pvr_hwrt_target *);
int pvr_hwrt_destroy(struct pvr_hwrt **);
int pvr_hwrt_reconstruct(struct pvr_hwrt *);

#endif /* _PVR_HWRT_H_ */
