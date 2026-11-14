/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Module glue of the GPU scheduler of Linux 6.18 (dist618/drm/scheduler):
 * what Linux does by module_init in sched_fence.c.  drm618_prefix.h turns
 * module_init(fn) and module_exit(fn) of the sources into functions
 * drm618_modinit_fn and drm618_modexit_fn that are called from here.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>

MODULE(MODULE_CLASS_DRIVER, drmkms618_sched, "drmkms618");

int	drm618_modinit_drm_sched_fence_slab_init(void);
void	drm618_modexit_drm_sched_fence_slab_fini(void);

static int
drmkms618_sched_modcmd(modcmd_t cmd, void *arg __unused)
{
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		error = drm618_modinit_drm_sched_fence_slab_init();
		return -error;	/* the 6.18 code returns Linux errnos */
	case MODULE_CMD_FINI:
		drm618_modexit_drm_sched_fence_slab_fini();
		return 0;
	default:
		return ENOTTY;
	}
}
