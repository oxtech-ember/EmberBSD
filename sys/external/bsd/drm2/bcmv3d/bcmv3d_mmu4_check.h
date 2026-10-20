/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_MMU4_CHECK_H_
#define _BCMV3D_MMU4_CHECK_H_
#include <sys/types.h>

struct bcmv3d_device;
struct bcmv3d_mmu4_native;

struct bcmv3d_mmu4_check_status {
	uint64_t before_bytes, after_bytes, reservations, releases;
	unsigned int cycles, staged, published, unmapped;
	int first_error;
	bool attempted, complete;
};

/*
 * Manual boot profile only, before userspace. Caller owns the actual renderer
 * and its native MMU4 owner, with three empty never-HVS-latched slots. No other
 * caller may use those slots or allocate BOs during this check. All established
 * clock, access and exclusion lifetimes remain held. Process context, no Giant
 * or ICAC lock. Two cycles use only IOVA8/16/24MiB. No HVS or pixel access.
 * A zeroed status is consumed once. The first error stops publication. Existing
 * discard rules retain exposed or uncertain resources. Status grants no access.
 */
int bcmv3d_mmu4_check_ranges(struct bcmv3d_device *,
    struct bcmv3d_mmu4_native *, struct bcmv3d_mmu4_check_status *);
#endif
