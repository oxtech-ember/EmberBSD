/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _PVR_HEAP_H_
#define _PVR_HEAP_H_

#include <sys/types.h>

struct drm_pvr_heap;
struct drm_pvr_ioctl_dev_query_args;

/* These helpers apply to the registered BXM-4-64 profile only. */
int pvr_heap_query(uint32_t, struct drm_pvr_ioctl_dev_query_args *);
const struct drm_pvr_heap *pvr_heap_find(uint64_t, uint64_t);

#endif /* _PVR_HEAP_H_ */
