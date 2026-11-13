/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface in
 * include/drm/drm_managed.h, no Linux code.
 *
 * Every resource of a drm_device is a node on dev->managed.resources.  A
 * node either owns memory (drmm_kmalloc; no release action) or holds an
 * action and one pointer for it (drmm_add_action).  New nodes go to the head
 * of the list, so walking the list from the head releases the newest first.
 * The list is protected by dev->managed.lock; actions run without it.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/bug.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <drm/drm_device.h>
#include <drm/drm_managed.h>

#include "../dist618/drm/drm_internal.h"	/* drm_managed_release, drmm_add_final_kfree */

struct drmres {
	struct list_head	node;
	drmres_release_t	release;	/* NULL for plain memory */
	const char		*name;
	size_t			size;
	/* The caller's memory starts here, aligned as kmalloc memory is (8 bytes). */
	char			data[] __aligned(8);
};

static struct drmres *
drmres_alloc(size_t size, gfp_t gfp)
{
	struct drmres *dr;

	if (size > SIZE_MAX - sizeof(*dr))
		return NULL;
	dr = kmalloc(sizeof(*dr) + size, gfp);
	if (dr == NULL)
		return NULL;
	INIT_LIST_HEAD(&dr->node);
	dr->release = NULL;
	dr->name = NULL;
	dr->size = size;
	if (gfp & __GFP_ZERO)
		memset(dr->data, 0, size);
	return dr;
}

static void
drmres_add(struct drm_device *dev, struct drmres *dr)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->managed.lock, flags);
	list_add(&dr->node, &dev->managed.resources);
	spin_unlock_irqrestore(&dev->managed.lock, flags);
}

/* Run the node's action, if any, and free it.  The node is off the list. */
static void
drmres_release(struct drm_device *dev, struct drmres *dr)
{

	if (dr->release != NULL)
		(*dr->release)(dev, *(void **)dr->data);
	kfree(dr);
}

void
drm_managed_release(struct drm_device *dev)
{
	struct drmres *dr;
	unsigned long flags;

	/*
	 * Take one node at a time from the head, newest first, and run its
	 * action without the lock: an action may register or free resources
	 * of the same device, and what it registers is released too.
	 */
	for (;;) {
		spin_lock_irqsave(&dev->managed.lock, flags);
		if (list_empty(&dev->managed.resources)) {
			spin_unlock_irqrestore(&dev->managed.lock, flags);
			break;
		}
		dr = list_first_entry(&dev->managed.resources, struct drmres,
		    node);
		list_del(&dr->node);
		spin_unlock_irqrestore(&dev->managed.lock, flags);
		drmres_release(dev, dr);
	}

	/* The structure that contains the device goes last. */
	kfree(dev->managed.final_kfree);
	dev->managed.final_kfree = NULL;
}

void
drmm_add_final_kfree(struct drm_device *dev, void *container)
{

	dev->managed.final_kfree = container;
}

int
__drmm_add_action(struct drm_device *dev, drmres_release_t action,
    void *data, const char *name)
{
	struct drmres *dr;

	dr = drmres_alloc(sizeof(void *), GFP_KERNEL);
	if (dr == NULL)
		return -ENOMEM;
	dr->release = action;
	dr->name = name;
	*(void **)dr->data = data;
	drmres_add(dev, dr);
	return 0;
}

int
__drmm_add_action_or_reset(struct drm_device *dev, drmres_release_t action,
    void *data, const char *name)
{
	int error;

	error = __drmm_add_action(dev, action, data, name);
	if (error)
		(*action)(dev, data);
	return error;
}

void
drmm_release_action(struct drm_device *dev, drmres_release_t action,
    void *data)
{
	struct drmres *dr, *found = NULL;
	unsigned long flags;

	spin_lock_irqsave(&dev->managed.lock, flags);
	list_for_each_entry(dr, &dev->managed.resources, node) {
		if (dr->release == action && dr->size == sizeof(void *) &&
		    *(void **)dr->data == data) {
			list_del(&dr->node);
			found = dr;
			break;
		}
	}
	spin_unlock_irqrestore(&dev->managed.lock, flags);

	if (WARN_ON(found == NULL))
		return;
	drmres_release(dev, found);
}

void *
drmm_kmalloc(struct drm_device *dev, size_t size, gfp_t gfp)
{
	struct drmres *dr;

	dr = drmres_alloc(size, gfp);
	if (dr == NULL)
		return NULL;
	drmres_add(dev, dr);
	return dr->data;
}

char *
drmm_kstrdup(struct drm_device *dev, const char *s, gfp_t gfp)
{
	size_t size;
	char *p;

	if (s == NULL)
		return NULL;
	size = strlen(s) + 1;
	p = drmm_kmalloc(dev, size, gfp);
	if (p == NULL)
		return NULL;
	memcpy(p, s, size);
	return p;
}

void
drmm_kfree(struct drm_device *dev, void *data)
{
	struct drmres *dr, *found = NULL;
	unsigned long flags;

	if (data == NULL)
		return;

	spin_lock_irqsave(&dev->managed.lock, flags);
	list_for_each_entry(dr, &dev->managed.resources, node) {
		if (dr->release == NULL && (void *)dr->data == data) {
			list_del(&dr->node);
			found = dr;
			break;
		}
	}
	spin_unlock_irqrestore(&dev->managed.lock, flags);

	if (WARN_ON(found == NULL))
		return;
	kfree(found);
}

void
__drmm_mutex_release(struct drm_device *dev, void *res)
{
	struct mutex *lock = res;

	mutex_destroy(lock);
}

void
__drmm_workqueue_release(struct drm_device *dev, void *res)
{
	struct workqueue_struct *wq = res;

	destroy_workqueue(wq);
}
