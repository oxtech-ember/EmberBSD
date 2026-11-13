/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native module wrapper that runs the drm2 Linux
 * compatibility checks in ember/tests/drm2-linux-compat.c inside the kernel.
 * Loading succeeds only if every check passes; results go to the kernel log.
 */

#include <sys/cdefs.h>
#include <sys/module.h>
#include <sys/systm.h>

MODULE(MODULE_CLASS_MISC, drmkms_linux_test, "drmkms_linux");

int	drm2_linux_compat_run(void);
int	drm2_dma_resv_run(void);
int	drm2_linux_idr_run(void);
int	drm2_linux_sync_run(void);
int	drm2_linux_618_run(void);
int	drm2_linux_618_layer_run(void);
int	drm2_drm618_managed_run(void);
int	drm2_drm618_statics_run(void);
int	drm2_drm618_file_run(void);
int	drm2_drm618_kdev_run(void);
int	drm2_drm618_task_run(void);
int	drm2_drm618_gem_run(void);
int	drm2_drm618_kms_run(void);
int	drm2_drm618_hdr_run(void);
int	drm2_p4_layer_run(void);
int	drm618_file_fini(void);
void	drm618_sleep_fini(void);

static int
drmkms_linux_test_modcmd(modcmd_t cmd, void *arg __unused)
{
	int failed;

	switch (cmd) {
	case MODULE_CMD_INIT:
		failed = drm2_linux_compat_run();
		failed += drm2_dma_resv_run();
		failed += drm2_linux_idr_run();
		failed += drm2_linux_sync_run();
		failed += drm2_linux_618_run();
		failed += drm2_linux_618_layer_run();
		failed += drm2_drm618_managed_run();
		failed += drm2_drm618_statics_run();
		failed += drm2_drm618_file_run();
		failed += drm2_drm618_kdev_run();
		failed += drm2_drm618_task_run();
		failed += drm2_drm618_gem_run();
		failed += drm2_drm618_kms_run();
		failed += drm2_drm618_hdr_run();
		failed += drm2_p4_layer_run();
		if (failed != 0) {
			/*
			 * A module whose init fails is unloaded without its fini:
			 * give the per lwp keys of the layer (their destructors are
			 * in this module) back now.
			 */
			drm618_sleep_fini();
			(void)drm618_file_fini();
			return EIO;
		}
		return 0;
	case MODULE_CMD_FINI:
		/* the per lwp keys of the layer (and their destructors) live in this module */
		drm618_sleep_fini();
		return drm618_file_fini();
	default:
		return ENOTTY;
	}
}
