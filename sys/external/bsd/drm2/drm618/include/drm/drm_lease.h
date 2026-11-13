/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD clean-room implementation of the interface used by
 * MIT-licensed consumers; written without access to the GPL original.
 *
 * DRM leases: a master hands a subset of its display objects to another
 * client.  The owner of a device holds every object; a lessee holds only
 * what was leased to it.  EmberBSD does not implement the lease ioctls (see
 * drm618_lease.c), so there are never any lessees.
 */

#ifndef _DRM618_DRM_LEASE_H_
#define _DRM618_DRM_LEASE_H_

#include <linux/types.h>

struct drm_device;
struct drm_file;
struct drm_master;

/* The master at the top of the lessor chain: the owner of the device. */
struct drm_master *drm_lease_owner(struct drm_master *master);

/* Detach a lessee from its lessor and drop its lessor reference. */
void drm_lease_destroy(struct drm_master *lessee);

/* Does the client of file_priv hold object id?  The underscore form is the
 * one the core uses where the object type is already known to be leasable. */
bool drm_lease_held(struct drm_file *file_priv, int id);
bool _drm_lease_held(struct drm_file *file_priv, int id);

/* Take all objects back from the lessees of master. */
void drm_lease_revoke(struct drm_master *master);

/* The subset of the bit mask of CRTCs the client may use. */
uint32_t drm_lease_filter_crtcs(struct drm_file *file_priv, uint32_t crtcs);

/* ioctls of struct drm_ioctl_desc. */
int drm_mode_create_lease_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv);
int drm_mode_list_lessees_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv);
int drm_mode_get_lease_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv);
int drm_mode_revoke_lease_ioctl(struct drm_device *dev, void *data,
    struct drm_file *file_priv);

#endif	/* _DRM618_DRM_LEASE_H_ */
