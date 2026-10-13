/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */
#ifndef _SUN60I_A733_GPU_FWBRIDGE_H_
#define _SUN60I_A733_GPU_FWBRIDGE_H_

#include <sys/mutex.h>
#include <arm/sunxi/sun60i_a733_gpu_fwservice.h>
#include <external/bsd/drm2/powervr/pvr_backend.h>

struct sun60i_a733_gpu_fwbridge {
	struct sun60i_a733_gpu_fwservice service;
	struct pvr_backend backend;
	kmutex_t *lock;
	bool attempted, publishing, registered;
};

/*
 * Initialize zeroed storage once under the platform's adaptive mutex.
 * After attempted becomes true, exclude every diagnostic queue mutation.
 * Keep this object, its mutex, MMIO and all firmware resources until reboot.
 * Neither a failed attach nor a failed callback permits a reset or reclaim.
 */
int sun60i_a733_gpu_fwbridge_prepare(struct sun60i_a733_gpu_fwbridge *,
    kmutex_t *, bus_space_tag_t, bus_space_handle_t,
    struct sun60i_a733_gpu_fwmem *,
    const struct sun60i_a733_gpu_fwboot_result *,
    const struct sun60i_a733_gpu_fwprobe_result *);
/* Call without the platform lock. The DRM gate always precedes that lock. */
int sun60i_a733_gpu_fwbridge_publish(struct sun60i_a733_gpu_fwbridge *,
    struct drm_device *);

#endif /* _SUN60I_A733_GPU_FWBRIDGE_H_ */
