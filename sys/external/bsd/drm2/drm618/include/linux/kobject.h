/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of kobject calls in dist618/drm/i915
 * (gt/intel_gt_sysfs.c, i915_sysfs.c, i915_perf.c, display); the GPL original was not read.
 *
 * The shapes of the objects; the calls are declared, not defined (see
 * linux/sysfs.h).  kobj_to_dev goes through a declared function because a
 * struct device here is NetBSD's device and has no kobject.
 */
#ifndef _DRM618_LINUX_KOBJECT_H_
#define _DRM618_LINUX_KOBJECT_H_

#include <sys/types.h>

#include <linux/kref.h>
#include <linux/list.h>
#include <linux/sysfs.h>

struct device;
struct kobj_type;

struct kobject {
	const char		*name;
	struct list_head	entry;
	struct kobject		*parent;
	struct kref		kref;
	const struct kobj_type	*ktype;
};

struct kobj_type {
	void	(*release)(struct kobject *);
	const struct sysfs_ops *sysfs_ops;
	const struct attribute_group **default_groups;
};

enum kobject_action {
	KOBJ_ADD,
	KOBJ_REMOVE,
	KOBJ_CHANGE,
	KOBJ_MOVE,
	KOBJ_ONLINE,
	KOBJ_OFFLINE,
	KOBJ_BIND,
	KOBJ_UNBIND,
};

extern const struct sysfs_ops kobj_sysfs_ops;

void	kobject_init(struct kobject *, const struct kobj_type *);
int	kobject_add(struct kobject *, struct kobject *, const char *, ...)
	    __printflike(3, 4);
int	kobject_init_and_add(struct kobject *, const struct kobj_type *,
	    struct kobject *, const char *, ...) __printflike(4, 5);
struct kobject *kobject_create_and_add(const char *, struct kobject *);
struct kobject *kobject_get(struct kobject *);
void	kobject_put(struct kobject *);
void	kobject_del(struct kobject *);
int	kobject_uevent_env(struct kobject *, enum kobject_action, char *[]);

struct device	*drm618_kobj_to_dev(struct kobject *);
struct kobject	*drm618_kdev_kobj(struct device *);
#define	kobj_to_dev(kobj)	drm618_kobj_to_dev(kobj)

#endif
