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

#ifndef DRM2_COMPAT_HOST
#include <linux/array_size.h>
#include <linux/container_of.h>
#include <linux/limits.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/sprintf.h>
#include <linux/stdarg.h>
#include <linux/wordpart.h>
#endif

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

#ifndef DRM2_COMPAT_HOST
struct t_cof {
	int	a;
	long	b;
};

static int __printflike(3, 4)
t_scnprintf(char *buf, size_t size, const char *fmt, ...)
{
	va_list va;
	int n;

	va_start(va, fmt);
	n = vscnprintf(buf, size, fmt, va);
	va_end(va);
	return n;
}

static void
test_kernel_forwarders(void)
{
	int arr[7] = { 4, 9, 2, 7, 1, 8, 3 };
	struct t_cof s0;
	const long *cp = &s0.b;
	char buf[8];
	int x = 3, y = 5, n;

	CHECK(ARRAY_SIZE(arr) == 7);

	CHECK(container_of(&s0.b, struct t_cof, b) == &s0);
	CHECK(container_of_const(cp, struct t_cof, b) == &s0);
	CHECK(container_of_const(&s0.b, struct t_cof, b) == &s0);

	CHECK(U8_MAX == 255 && S8_MAX == 127 && S8_MIN == -128);
	CHECK(U16_MAX == 65535 && S16_MAX == 32767 && S16_MIN == -32768);
	CHECK(U32_MAX == 0xffffffffU && S32_MAX == 2147483647);
	CHECK(S32_MIN == -2147483647 - 1);
	CHECK(U64_MAX == 0xffffffffffffffffULL);
	CHECK(S64_MAX == 0x7fffffffffffffffLL);
	CHECK(S64_MIN == -0x7fffffffffffffffLL - 1);
	CHECK(INT_MAX == 2147483647 && UINT_MAX == 0xffffffffU);
	CHECK(LONG_MAX > 0 && ULONG_MAX > LONG_MAX);
	CHECK(USHRT_MAX == 65535 && SHRT_MAX == 32767);

	CHECK(DIV_ROUND_UP(7, 2) == 4 && DIV_ROUND_UP(8, 2) == 4);
	CHECK(DIV_ROUND_UP_ULL(0xffffffffffULL, 0x10) == 0x1000000000ULL);
	CHECK(DIV_ROUND_DOWN_ULL(7, 2) == 3);
	CHECK(DIV_ROUND_CLOSEST(7, 2) == 4 && DIV_ROUND_CLOSEST(-7, 2) == -4);
	CHECK(DIV_ROUND_CLOSEST(6, 4) == 2 && DIV_ROUND_CLOSEST(5, 4) == 1);
	CHECK(round_up(5, 4) == 8 && round_up(8, 4) == 8);
	CHECK(round_down(7, 4) == 4 && round_down(8, 4) == 8);
	CHECK(roundup(5, 3) == 6 && rounddown(7, 3) == 6);
	CHECK(abs(-3) == 3 && abs(3) == 3 && abs(0) == 0);
	CHECK(abs(-5000000000LL) == 5000000000LL);
	CHECK(abs(-3000000000L) == 3000000000L || sizeof(long) == 4);
	CHECK(abs_diff(3, 10) == 7 && abs_diff(10, 3) == 7);
	CHECK(abs_diff(3u, 4000000000u) == 3999999997u);
	CHECK(mult_frac(10, 3, 4) == 7);
	CHECK(int_sqrt(0) == 0 && int_sqrt(1) == 1 && int_sqrt(15) == 3);
	CHECK(int_sqrt(16) == 4 && int_sqrt(1000000) == 1000);
	CHECK(int_sqrt(0xffffffffUL) == 65535);
	for (n = 0; n < 70000; n += 37) {
		unsigned long r = (unsigned long)n;

		CHECK(int_sqrt(r * r) == r);
		if (r > 0)
			CHECK(int_sqrt(r * r - 1) == r - 1);
	}
	CHECK(reciprocal_scale(0x80000000U, 10) == 5);
	CHECK(reciprocal_scale(0xffffffffU, 10) == 9);

	CHECK(min(3, 5) == 3 && max(3, 5) == 5);
	CHECK(min3(4, 2, 6) == 2 && max3(4, 2, 6) == 6);
	CHECK(clamp(7, 1, 5) == 5 && clamp(-1, 1, 5) == 1 && clamp(3, 1, 5) == 3);
	CHECK(min_t(u8, 300, 7) == 7 && max_t(u8, 300, 7) == 44);
	CHECK(clamp_t(int, 9, 0, 4) == 4);
	CHECK(clamp_val(9, 0, 4) == 4);
	CHECK(min_not_zero(0, 5) == 5 && min_not_zero(3, 0) == 3);
	CHECK(min_not_zero(3, 5) == 3 && min_not_zero(0, 0) == 0);
	CHECK(umin(3, 5) == 3 && umax(3, 5) == 5);
	CHECK(umin(-1, 5u) == 5u && umax(-1, 5u) == 0xffffffffffffffffULL);
	CHECK(min_array(arr, ARRAY_SIZE(arr)) == 1);
	CHECK(max_array(arr, ARRAY_SIZE(arr)) == 9);
	swap(x, y);
	CHECK(x == 5 && y == 3);

	CHECK(upper_32_bits(0x123456789abcdef0ULL) == 0x12345678U);
	CHECK(lower_32_bits(0x123456789abcdef0ULL) == 0x9abcdef0U);
	CHECK(upper_16_bits(0x12345678U) == 0x1234);
	CHECK(lower_16_bits(0x12345678U) == 0x5678);
	CHECK(REPEAT_BYTE(0xab) == (unsigned long)0xabababababababab ||
	    sizeof(long) == 4);
	CHECK(REPEAT_BYTE_U32(0xab) == 0xababababU);

	n = t_scnprintf(buf, sizeof(buf), "%d-%s", 42, "abcdefgh");
	CHECK(n == 7);			/* truncated, returns bytes stored */
	CHECK_STR(buf, "42-abcd");
	n = scnprintf(buf, sizeof(buf), "%x", 255);
	CHECK(n == 2);
	CHECK_STR(buf, "ff");
	CHECK(scnprintf(buf, 0, "x") == 0);
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
	test_string_choices();
	test_units();
	test_util_macros();
#ifndef DRM2_COMPAT_HOST
	test_kernel_forwarders();
#endif
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
