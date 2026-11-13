/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 include/linux/cgroup_dmem.h
 * (MIT; no source copied).
 *
 * Device memory cgroup accounting, disabled variant: regions are not
 * registered (NULL) and every charge succeeds.  The interface is what the
 * Linux header provides without CONFIG_CGROUP_DMEM; TTM and drm_drv only
 * call these.
 */

#ifndef _CGROUP_DMEM_H
#define _CGROUP_DMEM_H

#include <sys/types.h>

#include <linux/types.h>

struct dmem_cgroup_pool_state;
struct dmem_cgroup_region;

static inline __printflike(2, 3) struct dmem_cgroup_region *
dmem_cgroup_register_region(u64 size __unused, const char *name_fmt __unused,
    ...)
{

	return NULL;
}

static inline void
dmem_cgroup_unregister_region(struct dmem_cgroup_region *region __unused)
{
}

static inline int
dmem_cgroup_try_charge(struct dmem_cgroup_region *region __unused,
    u64 size __unused, struct dmem_cgroup_pool_state **ret_pool,
    struct dmem_cgroup_pool_state **ret_limit_pool)
{

	*ret_pool = NULL;
	if (ret_limit_pool != NULL)
		*ret_limit_pool = NULL;
	return 0;
}

static inline void
dmem_cgroup_uncharge(struct dmem_cgroup_pool_state *pool __unused,
    u64 size __unused)
{
}

static inline bool
dmem_cgroup_state_evict_valuable(struct dmem_cgroup_pool_state *limit_pool __unused,
    struct dmem_cgroup_pool_state *test_pool __unused,
    bool ignore_low __unused, bool *ret_hit_low __unused)
{

	return true;
}

static inline void
dmem_cgroup_pool_state_put(struct dmem_cgroup_pool_state *pool __unused)
{
}

#endif	/* _CGROUP_DMEM_H */
