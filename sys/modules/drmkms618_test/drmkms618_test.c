/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native module wrapper that runs the checks of the 6.18
 * helpers (scheduler, TTM) inside the kernel.  Loading succeeds only if every
 * check passes; results go to the kernel log.
 */

#include <sys/cdefs.h>
#include <sys/module.h>
#include <sys/systm.h>

MODULE(MODULE_CLASS_MISC, drmkms618_test, "drmkms618,drmkms618_sched,drmkms618_ttm");

int	drm2_sched_run(void);
int	drm2_ttm_run(void);

static int
drmkms618_test_modcmd(modcmd_t cmd, void *arg __unused)
{
	int failed;

	switch (cmd) {
	case MODULE_CMD_INIT:
		failed = drm2_sched_run();
		failed += drm2_ttm_run();
		return failed != 0 ? EIO : 0;
	case MODULE_CMD_FINI:
		return 0;
	default:
		return ENOTTY;
	}
}
