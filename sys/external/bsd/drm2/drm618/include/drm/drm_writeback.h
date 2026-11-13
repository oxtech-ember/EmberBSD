/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Writeback connectors: a connector whose "display" is a framebuffer the
 * hardware writes the composed output to.  EmberBSD implements only the
 * entry points the atomic core calls (drm618_unsupported.c: there is no
 * writeback hardware support); the rest are declared for drivers that use
 * them.
 */

#ifndef _DRM618_DRM_WRITEBACK_H_
#define _DRM618_DRM_WRITEBACK_H_

#include <linux/container_of.h>
#include <linux/dma-fence.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>

struct drm_atomic_state;
struct drm_connector_funcs;
struct drm_device;
struct drm_encoder_helper_funcs;
struct drm_framebuffer;
struct drm_property_blob;

struct drm_writeback_connector {
	struct drm_connector base;
	struct drm_encoder encoder;
	struct drm_property_blob *pixel_formats_blob_ptr;

	/* job_queue holds the struct drm_writeback_job in flight. */
	spinlock_t job_lock;
	struct list_head job_queue;
};

/* One request to write a commit's output to fb. */
struct drm_writeback_job {
	struct drm_writeback_connector *connector;
	bool prepared;			/* the driver's prepare hook has run */
	struct list_head list_entry;	/* on connector->job_queue */
	struct drm_framebuffer *fb;
	struct dma_fence *out_fence;	/* signalled when the write is done */
	void *priv;			/* the driver's */
};

static inline struct drm_writeback_connector *
drm_connector_to_writeback(struct drm_connector *connector)
{

	return container_of(connector, struct drm_writeback_connector, base);
}

int drm_writeback_connector_init(struct drm_device *dev,
    struct drm_writeback_connector *wb_connector,
    const struct drm_connector_funcs *con_funcs,
    const struct drm_encoder_helper_funcs *enc_helper_funcs,
    const u32 *formats, int n_formats, u32 possible_crtcs);

int drm_writeback_set_fb(struct drm_connector_state *conn_state,
    struct drm_framebuffer *fb);
int drm_writeback_prepare_job(struct drm_writeback_job *job);
void drm_writeback_queue_job(struct drm_writeback_connector *wb_connector,
    struct drm_connector_state *conn_state);
void drm_writeback_cleanup_job(struct drm_writeback_job *job);
void drm_writeback_signal_completion(
    struct drm_writeback_connector *wb_connector, int status);
struct dma_fence *drm_writeback_get_out_fence(
    struct drm_writeback_connector *wb_connector);

#endif	/* _DRM618_DRM_WRITEBACK_H_ */
