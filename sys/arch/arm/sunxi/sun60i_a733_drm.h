/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native A733 KMS state. */
#ifndef _SUN60I_A733_DRM_H
#define _SUN60I_A733_DRM_H

#include <sys/bus.h>
#include <sys/callout.h>
#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/vmem.h>
#include <sys/workqueue.h>
#include <drm/drm_device.h>
#include <drm/drm_plane.h>
#include <drm/drm_crtc.h>
#include <drm/drm_encoder.h>
#include <drm/drm_connector.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_cma_helper.h>
#include <arm/sunxi/sun60i_a733_de352_scanout.h>
#include <arm/sunxi/sun60i_a733_vblank_history.h>

/* Four 1920x1080 XRGB8888 dumb buffers fit in this reservation. */
#define A733_DRM_POOL_BYTES (UINT64_C(32) << 20)
#define A733_DRM_POOL_LIMIT (UINT64_C(1) << 32)

#define A733_DRM_SHADOW_SLOTS 4

struct a733_shadow_slot {
	struct a733_drm *sc;
	struct drm_gem_cma_object *object;
	bool claimed, retained;
};
struct sun60i_a733_drm_frame;
struct a733_drm_file {
	uint64_t id;
};
struct a733_drm {
	device_t device;
	kmutex_t lock;
	kcondvar_t idle;
	/* Serializes the fit proof and the CMA allocation. */
	kmutex_t pool_lock;
	bus_dma_tag_t pool_tag;
	bus_dma_segment_t pool_segment;
	vmem_t *pool;
	struct workqueue *queue;
	struct cpu_info *worker_cpu;
	kmutex_t startup_lock;
	callout_t startup_timer;
	struct work startup_work;
	bool startup_queued, startup_running, startup_registered, startup_stopped;
	struct drm_device *dev;
	struct drm_plane plane;
	uint64_t next_file_id;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct sun60i_a733_de352_state native;
	struct sun60i_a733_de352_client *client;
	struct sysctllog *sysctllog;
	int phandle, stage, fault;
	bool busy, registering, registration_started, shadow_enabled;
	struct a733_shadow_slot shadows[A733_DRM_SHADOW_SLOTS];
	/*
	 * TCON_TV0 vblank count from the interrupt callback. vblank_irq is
	 * set before drm_dev_register and is then constant. vblank_on is
	 * used only by the serialized commit worker.
	 */
	uint64_t hw_vblank;
	kmutex_t vblank_history_lock;
	struct a733_vblank_history vblank_history;
	bool vblank_irq, vblank_on;
};

struct a733_drm_fb {
	struct drm_framebuffer base;
	struct drm_gem_cma_object *object;
	uint64_t owner_id;
};

struct drm_gem_cma_object *a733_drm_alloc_cma(struct a733_drm *, size_t);
bool a733_drm_mode_matches(const struct sun60i_a733_output_mode *,
    const struct drm_display_mode *);
int a733_drm_atomic_check(struct drm_device *, struct drm_atomic_state *);
int a733_drm_atomic_commit(struct drm_device *, struct drm_atomic_state *, bool);
void a733_drm_vblank_history_reset(struct a733_drm *, bool);
bool a733_drm_vblank_stamp(struct a733_drm *, uint64_t,
    struct a733_vblank_stamp *);
void a733_drm_work(struct work *, void *);

#endif
