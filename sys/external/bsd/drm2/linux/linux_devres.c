/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * Device-managed resources (devm_*) for the Linux layer.  A NetBSD device_t
 * has no list to hang them on, so they are kept in one global list tagged
 * with the device; devres_release_all(dev) (called from a driver's detach,
 * or by whoever owns the device) releases the device's resources, newest
 * first.  Actions run without the lock.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/types.h>
#include <sys/once.h>
#include <sys/systm.h>

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/list.h>

struct devres_node {
	struct list_head	node;
	struct device		*dev;
	void			(*action)(void *);
	void			*data;
	size_t			size;	/* memory resources: user size */
	/* memory follows */
};

static struct {
	kmutex_t		lock;
	struct list_head	list;
} devres_state;
static ONCE_DECL(devres_once);

static int
devres_init(void)
{

	mutex_init(&devres_state.lock, MUTEX_DEFAULT, IPL_NONE);
	INIT_LIST_HEAD(&devres_state.list);
	return 0;
}

static inline void
devres_init_once(void)
{

	(void)RUN_ONCE(&devres_once, &devres_init);
}

static struct devres_node *
devres_new(struct device *dev, size_t size, void (*action)(void *),
    void *data)
{
	struct devres_node *dn;

	devres_init_once();
	dn = kmem_zalloc(sizeof(*dn) + size, KM_SLEEP);
	dn->dev = dev;
	dn->action = action;
	dn->data = data;
	dn->size = size;
	mutex_enter(&devres_state.lock);
	list_add(&dn->node, &devres_state.list);
	mutex_exit(&devres_state.lock);
	return dn;
}

int
devm_add_action(struct device *dev, void (*action)(void *), void *data)
{

	(void)devres_new(dev, 0, action, data);
	return 0;
}

int
devm_add_action_or_reset(struct device *dev, void (*action)(void *),
    void *data)
{
	int error = devm_add_action(dev, action, data);

	if (error)
		(*action)(data);
	return error;
}

void
devm_release_action(struct device *dev, void (*action)(void *), void *data)
{
	struct devres_node *dn, *found = NULL;

	devres_init_once();
	mutex_enter(&devres_state.lock);
	list_for_each_entry(dn, &devres_state.list, node) {
		if (dn->dev == dev && dn->action == action && dn->data == data &&
		    dn->size == 0) {
			list_del(&dn->node);
			found = dn;
			break;
		}
	}
	mutex_exit(&devres_state.lock);
	if (found == NULL)
		return;
	(*found->action)(found->data);
	kmem_free(found, sizeof(*found));
}

void *
devm_kmalloc(struct device *dev, size_t size, gfp_t gfp)
{
	struct devres_node *dn;

	dn = devres_new(dev, size, NULL, NULL);
	if (dn == NULL)
		return NULL;
	(void)gfp;	/* zeroed always: a superset of what is asked */
	return dn + 1;
}

void
devm_kfree(struct device *dev, void *p)
{
	struct devres_node *dn, *found = NULL;

	if (p == NULL)
		return;
	mutex_enter(&devres_state.lock);
	list_for_each_entry(dn, &devres_state.list, node) {
		if (dn->dev == dev && dn->action == NULL && (void *)(dn + 1) == p) {
			list_del(&dn->node);
			found = dn;
			break;
		}
	}
	mutex_exit(&devres_state.lock);
	KASSERTMSG(found != NULL, "devm_kfree(%p): not a resource of the device", p);
	if (found != NULL)
		kmem_free(found, sizeof(*found) + found->size);
}

void
devres_release_all(struct device *dev)
{
	struct devres_node *dn;

	devres_init_once();
	for (;;) {
		mutex_enter(&devres_state.lock);
		dn = NULL;
		{
			struct devres_node *it;

			list_for_each_entry(it, &devres_state.list, node) {
				if (it->dev == dev) {
					dn = it;
					break;
				}
			}
		}
		if (dn != NULL)
			list_del(&dn->node);
		mutex_exit(&devres_state.lock);
		if (dn == NULL)
			break;
		if (dn->action != NULL)
			(*dn->action)(dn->data);
		kmem_free(dn, sizeof(*dn) + dn->size);
	}
}
