/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Module glue of TTM of Linux 6.18 (dist618/drm/ttm): the static objects of
 * the sources (drm618_statics.h) and the global state of the pool.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>

MODULE(MODULE_CLASS_DRIVER, drmkms618_ttm, "drmkms618");

void	drm618_static_init_ttm_global_mutex(void);
void	drm618_static_fini_ttm_global_mutex(void);

static int
drmkms618_ttm_modcmd(modcmd_t cmd, void *arg __unused)
{

	switch (cmd) {
	case MODULE_CMD_INIT:
		drm618_static_init_ttm_global_mutex();
		return 0;
	case MODULE_CMD_FINI:
		drm618_static_fini_ttm_global_mutex();
		return 0;
	default:
		return ENOTTY;
	}
}
