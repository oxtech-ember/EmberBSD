/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * Resources tied to the life of a struct drm_device: memory and cleanup
 * actions registered here are released, newest first, when the last
 * reference to the device goes away.  Implemented in drm618_managed.c.
 */

#ifndef _DRM618_DRM_MANAGED_H_
#define _DRM618_DRM_MANAGED_H_

#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/workqueue.h>

struct drm_device;

/* A cleanup action: called with the device and the pointer it was added with. */
typedef void (*drmres_release_t)(struct drm_device *dev, void *res);

int __drmm_add_action(struct drm_device *dev, drmres_release_t action,
    void *data, const char *name);
int __drmm_add_action_or_reset(struct drm_device *dev,
    drmres_release_t action, void *data, const char *name);

#define drmm_add_action(dev, action, data) \
	__drmm_add_action((dev), (action), (data), #action)

/* Like drmm_add_action, but runs the action at once if it cannot be added. */
#define drmm_add_action_or_reset(dev, action, data) \
	__drmm_add_action_or_reset((dev), (action), (data), #action)

/* Run the action now and forget it; it must have been added with this data. */
void drmm_release_action(struct drm_device *dev, drmres_release_t action,
    void *data);

void *drmm_kmalloc(struct drm_device *dev, size_t size, gfp_t gfp)
    __malloc;
char *drmm_kstrdup(struct drm_device *dev, const char *s, gfp_t gfp);
void drmm_kfree(struct drm_device *dev, void *data);

static inline void *
drmm_kzalloc(struct drm_device *dev, size_t size, gfp_t gfp)
{

	return drmm_kmalloc(dev, size, gfp | __GFP_ZERO);
}

static inline void *
drmm_kmalloc_array(struct drm_device *dev, size_t n, size_t size, gfp_t gfp)
{
	size_t total;

	if (__builtin_mul_overflow(n, size, &total))
		return NULL;
	return drmm_kmalloc(dev, total, gfp);
}

static inline void *
drmm_kcalloc(struct drm_device *dev, size_t n, size_t size, gfp_t gfp)
{

	return drmm_kmalloc_array(dev, n, size, gfp | __GFP_ZERO);
}

void __drmm_mutex_release(struct drm_device *dev, void *res);
void __drmm_workqueue_release(struct drm_device *dev, void *res);

/* Initialise a mutex that is destroyed with the device; 0 or -errno. */
#define drmm_mutex_init(dev, lock) ({					\
	struct mutex *drmm_lock__ = (lock);				\
	mutex_init(drmm_lock__);					\
	drmm_add_action_or_reset((dev), __drmm_mutex_release,		\
	    drmm_lock__);						\
})

/* An ordered workqueue destroyed with the device; ERR_PTR(-errno) on failure. */
#define drmm_alloc_ordered_workqueue(dev, fmt, flags, args...) ({	\
	struct workqueue_struct *drmm_wq__ =				\
	    alloc_ordered_workqueue(fmt, flags, ##args);		\
	int drmm_err__;							\
									\
	if (drmm_wq__ == NULL) {					\
		drmm_wq__ = ERR_PTR(-ENOMEM);				\
	} else {							\
		drmm_err__ = drmm_add_action_or_reset((dev),		\
		    __drmm_workqueue_release, drmm_wq__);		\
		if (drmm_err__)						\
			drmm_wq__ = ERR_PTR(drmm_err__);		\
	}								\
	drmm_wq__;							\
})

#endif	/* _DRM618_DRM_MANAGED_H_ */
