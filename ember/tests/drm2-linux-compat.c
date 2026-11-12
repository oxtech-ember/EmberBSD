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

#include <linux/string_choices.h>
#include <linux/string_helpers.h>
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
test_string_choices(void)
{

	CHECK_STR(str_assert_deassert(true), "assert");
	CHECK_STR(str_assert_deassert(false), "deassert");
	CHECK_STR(str_deassert_assert(true), "deassert");
	CHECK_STR(str_deassert_assert(false), "assert");
	CHECK_STR(str_enable_disable(true), "enable");
	CHECK_STR(str_enable_disable(false), "disable");
	CHECK_STR(str_disable_enable(true), "disable");
	CHECK_STR(str_disable_enable(false), "enable");
	CHECK_STR(str_enabled_disabled(true), "enabled");
	CHECK_STR(str_enabled_disabled(false), "disabled");
	CHECK_STR(str_disabled_enabled(true), "disabled");
	CHECK_STR(str_disabled_enabled(false), "enabled");
	CHECK_STR(str_hi_lo(true), "hi");
	CHECK_STR(str_hi_lo(false), "lo");
	CHECK_STR(str_lo_hi(true), "lo");
	CHECK_STR(str_lo_hi(false), "hi");
	CHECK_STR(str_high_low(true), "high");
	CHECK_STR(str_high_low(false), "low");
	CHECK_STR(str_low_high(true), "low");
	CHECK_STR(str_low_high(false), "high");
	CHECK_STR(str_input_output(true), "input");
	CHECK_STR(str_input_output(false), "output");
	CHECK_STR(str_output_input(true), "output");
	CHECK_STR(str_output_input(false), "input");
	CHECK_STR(str_on_off(true), "on");
	CHECK_STR(str_on_off(false), "off");
	CHECK_STR(str_off_on(true), "off");
	CHECK_STR(str_off_on(false), "on");
	CHECK_STR(str_yes_no(true), "yes");
	CHECK_STR(str_yes_no(false), "no");
	CHECK_STR(str_no_yes(true), "no");
	CHECK_STR(str_no_yes(false), "yes");
	CHECK_STR(str_true_false(true), "true");
	CHECK_STR(str_true_false(false), "false");
	CHECK_STR(str_false_true(true), "false");
	CHECK_STR(str_false_true(false), "true");
	CHECK_STR(str_up_down(true), "up");
	CHECK_STR(str_up_down(false), "down");
	CHECK_STR(str_down_up(true), "down");
	CHECK_STR(str_down_up(false), "up");
	CHECK_STR(str_read_write(true), "read");
	CHECK_STR(str_read_write(false), "write");
	CHECK_STR(str_write_read(true), "write");
	CHECK_STR(str_write_read(false), "read");
	/* Pointers to the same text are interchangeable in constant contexts. */
	CHECK(str_yes_no(1) == str_yes_no(true));
	CHECK_STR(str_plural(0), "s");
	CHECK_STR(str_plural(1), "");
	CHECK_STR(str_plural(2), "s");
	CHECK_STR(str_plural((size_t)-1), "s");
}

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
	test_string_choices();
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
