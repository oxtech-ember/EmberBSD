/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Written to the interface of
 * drm_lease.h (functions and their documented contracts as the MIT callers
 * use them) and the structure drm_master of drm_auth.h; the GPL-only upstream
 * drm_lease.c was not read.
 *
 * DRM leases (handing a part of the display objects of the master to
 * another client) are not provided: no lease is ever created, so every
 * master is its own owner, holds every object it sees, and the lease
 * ioctls say they are not supported.  The callers (drm_auth.c, drm_crtc.c,
 * drm_encoder.c, drm_mode_config.c) behave as on a system without leases.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/errno.h>

#include <drm/drm_auth.h>
#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_lease.h>

struct drm_master *
drm_lease_owner(struct drm_master *master)
{

	while (master->lessor != NULL)
		master = master->lessor;
	return master;
}

void
drm_lease_destroy(struct drm_master *lessee __unused)
{

	/* No lessee exists: nothing to take out of the lessor's list. */
}

bool
_drm_lease_held(struct drm_file *file_priv, int id __unused)
{

	if (file_priv == NULL || file_priv->master == NULL ||
	    file_priv->master->lessor == NULL)
		return true;	/* an owner holds everything */
	return false;		/* a lessee holds only what was leased: nothing */
}

bool
drm_lease_held(struct drm_file *file_priv, int id)
{

	return _drm_lease_held(file_priv, id);
}

void
drm_lease_revoke(struct drm_master *master __unused)
{

	/* No lease to revoke. */
}

uint32_t
drm_lease_filter_crtcs(struct drm_file *file_priv, uint32_t crtcs)
{

	if (file_priv == NULL || file_priv->master == NULL ||
	    file_priv->master->lessor == NULL)
		return crtcs;
	return 0;
}

int
drm_mode_create_lease_ioctl(struct drm_device *dev __unused,
    void *data __unused, struct drm_file *file_priv __unused)
{

	return -EOPNOTSUPP;
}

int
drm_mode_list_lessees_ioctl(struct drm_device *dev __unused,
    void *data __unused, struct drm_file *file_priv __unused)
{

	return -EOPNOTSUPP;
}

int
drm_mode_get_lease_ioctl(struct drm_device *dev __unused,
    void *data __unused, struct drm_file *file_priv __unused)
{

	return -EOPNOTSUPP;
}

int
drm_mode_revoke_lease_ioctl(struct drm_device *dev __unused,
    void *data __unused, struct drm_file *file_priv __unused)
{

	return -EOPNOTSUPP;
}
