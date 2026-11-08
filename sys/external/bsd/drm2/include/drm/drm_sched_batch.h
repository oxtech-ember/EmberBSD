/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Native synchronous-batch extension of drmkms_sched. */
#ifndef _DRM_SCHED_BATCH_H_
#define _DRM_SCHED_BATCH_H_

#include <sys/types.h>
#include <drm/gpu_scheduler.h>

struct drm_file;
struct drm_syncobj;
struct dma_fence_chain;
struct drm_sched_batch_queue;
struct timespec;

/* Normalized syncobj operation. The common layer owns all retained pointers. */
struct drm_sched_batch_sync {
	uint32_t handle;
	uint64_t value;
	bool signal, timeline;
	struct drm_syncobj *object;
	struct dma_fence_chain *chain;
	struct dma_fence *wait;
};

struct drm_sched_batch_item {
	struct drm_sched_batch_sync *syncs;
	uint32_t sync_count;
	/* First item of the contiguous atomic execution unit containing this one. */
	uint32_t first;
	struct dma_fence *finished;
};

struct drm_sched_batch_ops {
	/* Exact intra-unit dependency implemented by the hardware protocol. */
	bool (*internal_dependency)(void *, uint32_t, uint32_t);
	/* May run without driver locks; referenced driver data must remain live. */
	bool (*cancelled)(void *, uint32_t);
	/* Synchronous, positive errno; return only after retirement/quarantine. */
	int (*execute)(void *, uint32_t);
	/* Sleeping cleanup, outside all scheduler/fence locks; frees the batch. */
	void (*release)(void *);
};

struct drm_sched_batch {
	struct drm_sched_job job;
	struct drm_sched_batch_queue *queue;
	const struct drm_sched_batch_ops *ops;
	void *data;
	struct drm_sched_batch_item *items;
	size_t stride, charged;
	uint32_t count;
};

int drm_sched_batch_queue_create(const char *, struct drm_sched_batch_queue **);
void drm_sched_batch_queue_stop(struct drm_sched_batch_queue *);
/* Owner keeps queue live for API entry; before destroy exclude new calls and
 * quiesce entrants not yet in reserve. Registered reserve waiters are joined. */
void drm_sched_batch_queue_destroy(struct drm_sched_batch_queue *);
bool drm_sched_batch_stopping(struct drm_sched_batch_queue *);
int drm_sched_batch_reserve(struct drm_sched_batch_queue *);
void drm_sched_batch_unreserve(struct drm_sched_batch_queue *);
void drm_sched_batch_deadline(struct timespec *);

int drm_sched_batch_size(uint32_t, size_t, size_t, size_t *);
int drm_sched_batch_init(struct drm_sched_batch *, struct drm_sched_batch_queue *,
    struct drm_sched_batch_item *, size_t, uint32_t, size_t,
    const struct drm_sched_batch_ops *, void *);
struct drm_sched_batch_item *drm_sched_batch_item(struct drm_sched_batch *, uint32_t);
int drm_sched_batch_sync_alloc(struct drm_sched_batch *, uint32_t, uint32_t);
int drm_sched_batch_prepare(struct drm_sched_batch *, struct drm_file *);
int drm_sched_batch_capture(struct drm_sched_batch *);
/* Success transfers ownership. Failure publishes nothing; caller cleans up. */
int drm_sched_batch_commit(struct drm_sched_batch *);
void drm_sched_batch_cleanup(struct drm_sched_batch *);

#endif
