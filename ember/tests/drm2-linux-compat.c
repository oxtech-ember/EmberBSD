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
#include <linux/units.h>
#include <linux/util_macros.h>

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
test_units(void)
{

	CHECK(HZ_PER_KHZ == 1000UL);
	CHECK(HZ_PER_MHZ == 1000000UL);
	CHECK(HZ_PER_GHZ == 1000000000UL);
	CHECK(KHZ_PER_MHZ == 1000UL);
	CHECK(KHZ_PER_GHZ == 1000000UL);
	CHECK(MILLIHZ_PER_HZ == 1000UL);
	CHECK(MICROHZ_PER_HZ == 1000000UL);
	CHECK(NANOHZ_PER_HZ == 1000000000UL);
	CHECK(MILLIWATT_PER_WATT == 1000UL);
	CHECK(MICROWATT_PER_MILLIWATT == 1000UL);
	CHECK(MICROWATT_PER_WATT == 1000000UL);
	CHECK(KILO == 1000UL && MEGA == 1000000UL && GIGA == 1000000000UL);
	CHECK(TERA == 1000000000000ULL && PETA == 1000000000000000ULL);
	CHECK(MILLI == 1000UL && MICRO == 1000000UL && NANO == 1000000000UL);
	CHECK(PICO == 1000000000000ULL && FEMTO == 1000000000000000ULL);
	CHECK(BYTES_PER_KBIT == 125UL);
	CHECK(BYTES_PER_MBIT == 125000UL);
	CHECK(BYTES_PER_GBIT == 125000000UL);
	CHECK(ABSOLUTE_ZERO_MILLICELSIUS == -273150);
	CHECK(MILLIDEGREE_PER_DEGREE == 1000);
	CHECK(MILLIDEGREE_PER_DECIDEGREE == 100);
	CHECK(milli_kelvin_to_millicelsius(273150) == 0);
	CHECK(millicelsius_to_milli_kelvin(0) == 273150);
	CHECK(kelvin_to_millicelsius(300) == 26850);
	CHECK(millicelsius_to_kelvin(26850) == 300);
	CHECK(millicelsius_to_kelvin(0) == 273);		/* 273.15 rounds down */
	CHECK(millicelsius_to_kelvin(25000) == 298);		/* 298.15 */
	CHECK(kelvin_to_celsius(300) == 27);			/* 26.85 rounds up */
	CHECK(celsius_to_kelvin(25) == 298);
	CHECK(deci_kelvin_to_millicelsius(3000) == 26850);
	CHECK(deci_kelvin_to_millicelsius_with_offset(3000, 0) == 300000);
	CHECK(millicelsius_to_deci_kelvin(26850) == 3000);
	CHECK(millicelsius_to_deci_kelvin(0) == 2732);		/* 2731.5 */
	CHECK(deci_kelvin_to_celsius(3000) == 27);
	CHECK(celsius_to_deci_kelvin(25) == 2982);		/* 2981.5 */
}

static void
test_util_macros(void)
{
	static const int asc[] = { 10, 20, 30, 50 };
	static const int desc[] = { 50, 30, 20, 10 };
	static const int odd[] = { 0, 3 };
	static const int neg[] = { -30, -20, -5, 0, 3 };
	int var[4], cnt = 0, i;
	static const char buf[8];

	CHECK(find_closest(0, asc, 4) == 0);
	CHECK(find_closest(10, asc, 4) == 0);
	CHECK(find_closest(14, asc, 4) == 0);
	CHECK(find_closest(16, asc, 4) == 1);
	CHECK(find_closest(26, asc, 4) == 2);
	CHECK(find_closest(39, asc, 4) == 2);
	CHECK(find_closest(41, asc, 4) == 3);
	CHECK(find_closest(1000, asc, 4) == 3);
	/* Midpoint of 20 and 30 is 25: x <= 25 picks the lower index. */
	CHECK(find_closest(25, asc, 4) == 1);
	/* Descending: on the boundary 25 the smaller element (index 2) wins. */
	CHECK(find_closest_descending(25, desc, 4) == 2);
	CHECK(find_closest_descending(15, desc, 4) == 3);
	CHECK(find_closest_descending(40, desc, 4) == 1);
	CHECK(find_closest_descending(1000, desc, 4) == 0);
	CHECK(find_closest_descending(41, desc, 4) == 0);
	CHECK(find_closest_descending(39, desc, 4) == 1);
	CHECK(find_closest_descending(26, desc, 4) == 1);
	CHECK(find_closest_descending(24, desc, 4) == 2);
	CHECK(find_closest_descending(0, desc, 4) == 3);
	CHECK(find_closest(5, asc, 1) == 0);
	/* Odd sum 0 + 3: 2 is nearer to 3, 1 is nearer to 0; no rounding. */
	CHECK(find_closest(1, odd, 2) == 0);
	CHECK(find_closest(2, odd, 2) == 1);
	CHECK(find_closest(2, neg, 5) == 4);
	CHECK(find_closest(-3, neg, 5) == 2);
	for (i = 0; i < 6; i++)
		for_each_if(i % 2 == 0)
			cnt++;
	CHECK(cnt == 3);
	CHECK(PTR_IF(true, buf) == buf);
	CHECK(PTR_IF(false, buf) == NULL);
	CHECK(is_insidevar(&var[0], var));
	CHECK(is_insidevar(&var[3], var));
	CHECK(!is_insidevar(&var[4], var));
	CHECK(!is_insidevar(&cnt, var));
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
	test_units();
	test_util_macros();
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
