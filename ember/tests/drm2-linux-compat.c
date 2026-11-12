/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native tests for the sys/external/bsd/drm2 Linux
 * compatibility layer (headers needed by the Linux 6.x DRM code).
 *
 * One body, two runners:
 *  - host: ember/tests/drm2-linux-compat.sh compiles it with
 *    -DDRM2_COMPAT_HOST against ember/tests/drm2-linux-compat-shim and runs
 *    it; only headers that depend on <linux/types.h> alone are tested there.
 *  - kernel: sys/modules/drmkms_linux_test builds it with the real layer
 *    headers; modload fails (EIO) and logs "FAIL" lines if a check fails.
 */

#ifdef DRM2_COMPAT_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#endif

#include <linux/types.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

#define	CHECK_STR(got, want)	do {					\
	t_run++;							\
	if (strcmp((got), (want)) != 0) {				\
		t_fail++;						\
		printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,	\
		    __LINE__, (got), (want));				\
	}								\
} while (0)

#ifdef DRM2_COMPAT_SELFTEST_FAIL
static void
test_selftest_fail(void)
{

	CHECK(1 == 2);
}
#endif

static void
test_harness(void)
{

	CHECK(1 == 1);
	CHECK_STR("a", "a");
}

int	drm2_linux_compat_run(void);

int
drm2_linux_compat_run(void)
{

	t_run = t_fail = 0;
	test_harness();
#ifdef DRM2_COMPAT_SELFTEST_FAIL
	test_selftest_fail();
#endif
	printf("drm2-linux-compat: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}

#ifdef DRM2_COMPAT_HOST
int
main(void)
{

	return drm2_linux_compat_run() == 0 ? 0 : 1;
}
#endif
