/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */
/* Origin: EmberBSD A733 firmware bridge, factored to the common owner. */
#ifndef _PVR_FWBRIDGE_H_
#define _PVR_FWBRIDGE_H_

#include <sys/mutex.h>
#include <dev/powervr/pvr_fwservice.h>
#include <external/bsd/drm2/powervr/pvr_backend.h>

struct pvr_fwbridge {
	struct pvr_fwservice service;
	struct pvr_backend backend;
	kmutex_t *lock;
	void *health_cookie;
	int (*health)(void *);
	unsigned (*irq_mark)(void *);
	void (*irq_wait)(void *, unsigned, int);
	bool attempted, publishing, registered;
};

/*
 * Initialize zeroed storage once under the platform's adaptive mutex.
 * After attempted becomes true, exclude every diagnostic queue mutation.
 * Keep this object, its mutex, MMIO and all firmware resources until reboot.
 * Neither a failed attach nor a failed callback permits a reset or reclaim.
 */
int pvr_fwbridge_prepare(struct pvr_fwbridge *,
    kmutex_t *, bus_space_tag_t, bus_space_handle_t,
    struct pvr_fwmem *,
    const struct pvr_fwboot_result *,
    const struct pvr_fwprobe_result *, enum pvr_fw_profile,
    int (*)(void *), void *);
/* Optional; call under the platform lock after prepare and before publish. */
void pvr_fwbridge_set_irq(struct pvr_fwbridge *, unsigned (*)(void *),
    void (*)(void *, unsigned, int));
/* Call without the platform lock. The DRM gate always precedes that lock. */
int pvr_fwbridge_publish(struct pvr_fwbridge *,
    struct drm_device *);

#endif /* _PVR_FWBRIDGE_H_ */
