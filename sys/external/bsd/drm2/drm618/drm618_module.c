/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Module glue of the 6.18 DRM core: what Linux does by module_init in the
 * files of the core (drm_core_init and two small ones) and by the layer
 * (statics).  drm618_prefix.h turns module_init(fn) and module_exit(fn) of
 * the sources into functions drm618_modinit_fn and drm618_modexit_fn that
 * are called from here.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <drm618_file.h>

int	drm618_dma_buf_busy(void);
#include <drm618_statics.h>

MODULE(MODULE_CLASS_DRIVER, drmkms618, "drmkms_linux,sysmon_power");

int	drm618_modinit_gpu_buddy_module_init(void);
void	drm618_modexit_gpu_buddy_module_exit(void);
int	drm618_modinit_drm_display_helper_module_init(void);
void	drm618_modexit_drm_display_helper_module_exit(void);
int	drm618_modinit_drm_core_init(void);
void	drm618_modexit_drm_core_exit(void);

static int
drmkms618_modcmd(modcmd_t cmd, void *arg __unused)
{
	int error;

	switch (cmd) {
	case MODULE_CMD_INIT:
		drm618_static_init();
		error = drm618_modinit_gpu_buddy_module_init();
		if (error)
			goto fail0;
		error = drm618_modinit_drm_display_helper_module_init();
		if (error)
			goto fail1;
		error = drm618_modinit_drm_core_init();
		if (error)
			goto fail2;
		return 0;
fail2:		drm618_modexit_drm_display_helper_module_exit();
fail1:		drm618_modexit_gpu_buddy_module_exit();
fail0:		drm618_static_fini();
		drm618_sleep_fini();
		(void)drm618_file_fini();
		return -error;	/* the 6.18 code returns Linux errnos */
	case MODULE_CMD_FINI:
		/* No driver of the core may be bound: they depend on this module. */
		error = drm618_vma_busy();
		if (error)
			return error;
		error = drm618_dma_buf_busy();
		if (error)
			return error;
		drm618_modexit_drm_core_exit();
		drm618_modexit_drm_display_helper_module_exit();
		drm618_modexit_gpu_buddy_module_exit();
		drm618_static_fini();
		drm618_sleep_fini();
		drm618_vma_fini();
		return drm618_file_fini();
	default:
		return ENOTTY;
	}
}
