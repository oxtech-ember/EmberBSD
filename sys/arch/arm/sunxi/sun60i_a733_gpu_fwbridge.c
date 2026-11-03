/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */
/* Origin: EmberBSD A733 firmware bridge compatibility adapter. */
#include <arm/sunxi/sun60i_a733_gpu_fwbridge.h>

int
sun60i_a733_gpu_fwbridge_prepare(struct pvr_fwbridge *b, kmutex_t *l,
    bus_space_tag_t t, bus_space_handle_t h, struct pvr_fwmem *m,
    const struct pvr_fwboot_result *boot,
    const struct pvr_fwprobe_result *previous)
{
	return pvr_fwbridge_prepare(b, l, t, h, m, boot, previous,
	    PVR_FW_PROFILE_A733, NULL, NULL);
}

int
sun60i_a733_gpu_fwbridge_publish(struct pvr_fwbridge *b, struct drm_device *d)
{
	return pvr_fwbridge_publish(b, d);
}
