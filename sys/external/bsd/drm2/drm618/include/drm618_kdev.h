/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface the
 * 6.18 DRM core uses, no Linux code.
 *
 * The class devices of the 6.18 core (minor->kdev, connector->kdev).  In
 * Linux they are struct device objects of the driver model, registered in
 * sysfs; here struct device is the opaque device_t of NetBSD, and a class
 * device is an object of the layer (drm618_sysfs.c) behind a struct device
 * pointer, recognised by a magic number.  Only the calls the core makes on
 * them are provided: add, delete, is-registered, driver data, and the helpers
 * below for the few places that look inside one (local patches of dist618,
 * listed in dist618/patches.tsv).  There is no sysfs and no uevent: the
 * links and uevents are not made (drm618_kdev_uevent logs at debug level).
 */

#ifndef _DRM618_KDEV_H_
#define _DRM618_KDEV_H_

#include <sys/types.h>

struct device;

/* A class device named by fmt with the Linux device number devt (MKDEV); refcount 1. */
struct device *drm618_kdev_create(unsigned int devt, void *drvdata,
    const char *fmt, ...) __printflike(3, 4);

int	device_add(struct device *);
void	device_del(struct device *);
bool	device_is_registered(struct device *);
void	dev_set_drvdata(struct device *, void *);
void	*dev_get_drvdata(const struct device *);

unsigned int	drm618_kdev_devt(const struct device *);
const char	*drm618_kdev_name(const struct device *);
void		drm618_kdev_put(struct device *);
int		drm618_kdev_uevent(struct device *, char *envp[]);
int		drm618_kdev_create_link(struct device *, const char *);
void		drm618_kdev_remove_link(struct device *, const char *);

#endif	/* _DRM618_KDEV_H_ */
