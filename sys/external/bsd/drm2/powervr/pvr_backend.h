/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native interface. */
#ifndef _PVR_BACKEND_H_
#define _PVR_BACKEND_H_

#include <sys/types.h>

struct rogue_fwif_kccb_cmd;
struct rogue_fwif_freelist_gs_data;

/*
 * Install once while both gates are held. Keep cookie alive until reboot.
 * Callbacks run after a copied FWCCB event is consumed and its read confirmed.
 * They must not acquire either gate or call backend operations. Complete
 * may call fail(), which only marks state without locking or transport.
 * A successful prepare always receives one complete, including delta0 and
 * uncertain response completion. Nonzero callback returns are fatal.
 */
struct pvr_fw_events {
	void *cookie;
	int (*grow_prepare)(void *, uint32_t, struct rogue_fwif_freelist_gs_data *);
	int (*grow_complete)(void *, uint32_t, int);
};

struct pvr_fw_object {
	void *cpu;
	void *owner;
	size_t size;
	uint32_t address;
};

/*
 * Kernel-only platform contract. Positive native errno throughout.
 * The DRM device gate precedes enter(); leave() releases the platform gate.
 * Alloc creates a zeroed, coherent firmware object and invalidates its mapping.
 * The client owns its object descriptor and all firmware references to it.
 * Free requires acknowledged context cleanup and no jobs referencing the object.
 * Failed free retains the descriptor and backing memory. Failure never proves
 * that hardware is quiescent. A lost device retains all reachable resources.
 * Commands accept kernel-built FWIF bytes, never direct user pointers.
 * KICK consumption does not imply completion. Check the client UFO and health.
 * The platform owner and callbacks must outlive the registered DRM device.
 */
struct pvr_backend {
	void *cookie;
	void (*enter)(void *);
	void (*leave)(void *);
	int (*alloc)(void *, size_t, struct pvr_fw_object *);
	int (*free)(void *, struct pvr_fw_object *);
	void (*sync)(void *, struct pvr_fw_object *, int);
	int (*command)(void *, const struct rogue_fwif_kccb_cmd *, uint32_t *);
	int (*flush)(void *);
	int (*check)(void *);
	void (*fail)(void *);
	int (*set_events)(void *, const struct pvr_fw_events *);
};

struct drm_device;
int pvr_backend_attach(struct drm_device *, const struct pvr_backend *);
/* NULL means no execution backend. CPU-only VM operations remain available. */
const struct pvr_backend *pvr_device_enter(struct drm_device *);
void pvr_device_leave(struct drm_device *);
/* Retain unreachable device-owned state until reboot. The device gate is held. */
void pvr_device_retain(struct drm_device *, void *);
int pvr_device_next_id(struct drm_device *, uint32_t *);

#endif /* _PVR_BACKEND_H_ */
