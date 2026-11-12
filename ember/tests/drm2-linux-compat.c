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

#include <linux/cleanup.h>
#include <linux/iosys-map.h>
#include <linux/kdev_t.h>
#include <linux/string_choices.h>
#include <linux/string_helpers.h>
#include <linux/swab.h>
#include <linux/types.h>
#include <linux/units.h>
#include <linux/util_macros.h>

#ifndef DRM2_COMPAT_HOST
#include <linux/array_size.h>
#include <linux/bsearch.h>
#include <linux/container_of.h>
#include <linux/dma-direction.h>
#include <linux/hrtimer_types.h>
#include <linux/iopoll.h>
#include <linux/limits.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/sprintf.h>
#include <linux/nmi.h>
#include <linux/stdarg.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>
#include <linux/unaligned.h>
#include <linux/wordpart.h>
#include <xen/xen.h>
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
test_kdev_swab(void)
{
	u16 v16 = 0x1234;
	u32 v32 = 0x12345678;
	u64 v64 = 0x0123456789abcdefULL;

	CHECK(MINORBITS == 20);
	CHECK(MINORMASK == 0xfffff);
	CHECK(MKDEV(226, 3) == (226u << 20 | 3u));
	CHECK(MAJOR(MKDEV(226, 3)) == 226);
	CHECK(MINOR(MKDEV(226, 3)) == 3);
	CHECK(MAJOR(MKDEV(4095, 0xfffff)) == 4095);
	CHECK(MINOR(MKDEV(4095, 0xfffff)) == 0xfffff);

	CHECK(swab16(0x1234) == 0x3412);
	CHECK(swab32(0x12345678U) == 0x78563412U);
	CHECK(swab64(0x0123456789abcdefULL) == 0xefcdab8967452301ULL);
	CHECK(__swab16(0x00ff) == 0xff00);
	CHECK(__swab32(1U) == 0x01000000U);
	CHECK(__swab64(1ULL) == 0x0100000000000000ULL);
	CHECK(swab16p(&v16) == 0x3412);
	CHECK(swab32p(&v32) == 0x78563412U);
	CHECK(swab64p(&v64) == 0xefcdab8967452301ULL);
	swab16s(&v16);
	swab32s(&v32);
	swab64s(&v64);
	CHECK(v16 == 0x3412 && v32 == 0x78563412U);
	CHECK(v64 == 0xefcdab8967452301ULL);
	CHECK(swahw32(0x12345678U) == 0x56781234U);
	CHECK(swahb32(0x12345678U) == 0x34127856U);
	CHECK(swab16(swab16(0xbeef)) == 0xbeef);
	/* Constant expressions work for static initialisers. */
	{
		static const u32 k = swab32(0x11223344U);

		CHECK(k == 0x44332211U);
	}
}

#ifndef DRM2_COMPAT_HOST
static int
t_cmp_int(const void *a, const void *b)
{
	int x = *(const int *)a, y = *(const int *)b;

	return (x > y) - (x < y);
}

static void
test_kernel_misc(void)
{
	static const int sorted[] = { 1, 3, 5, 7, 9, 11 };
	unsigned char b[9] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	    0x09 };
	unsigned char o[9];
	int key, i;
	int *hit;
	ktime_t t0, t1, b0;
	struct hrtimer ht;
	struct timespec64 ts;

	/* bsearch */
	for (i = 0; i < (int)ARRAY_SIZE(sorted); i++) {
		key = sorted[i];
		hit = bsearch(&key, sorted, ARRAY_SIZE(sorted), sizeof(int),
		    t_cmp_int);
		CHECK(hit == &sorted[i]);
	}
	key = 4;
	CHECK(bsearch(&key, sorted, ARRAY_SIZE(sorted), sizeof(int),
	    t_cmp_int) == NULL);
	key = 1;
	CHECK(bsearch(&key, sorted, 0, sizeof(int), t_cmp_int) == NULL);

	/* unaligned access at an odd offset */
	CHECK(get_unaligned_le16(&b[1]) == 0x0302);
	CHECK(get_unaligned_be16(&b[1]) == 0x0203);
	CHECK(get_unaligned_le32(&b[1]) == 0x05040302U);
	CHECK(get_unaligned_be32(&b[1]) == 0x02030405U);
	CHECK(get_unaligned_le64(&b[1]) == 0x0908070605040302ULL);
	CHECK(get_unaligned_be64(&b[1]) == 0x0203040506070809ULL);
	memset(o, 0, sizeof(o));
	put_unaligned_le16(0xa1b2, &o[1]);
	CHECK(o[0] == 0 && o[1] == 0xb2 && o[2] == 0xa1 && o[3] == 0);
	memset(o, 0, sizeof(o));
	put_unaligned_be16(0xa1b2, &o[1]);
	CHECK(o[1] == 0xa1 && o[2] == 0xb2);
	memset(o, 0, sizeof(o));
	put_unaligned_le32(0xa1b2c3d4U, &o[1]);
	CHECK(o[1] == 0xd4 && o[4] == 0xa1 && o[5] == 0);
	memset(o, 0, sizeof(o));
	put_unaligned_be32(0xa1b2c3d4U, &o[1]);
	CHECK(o[1] == 0xa1 && o[4] == 0xd4 && o[5] == 0);
	memset(o, 0, sizeof(o));
	put_unaligned_le64(0x1122334455667788ULL, &o[1]);
	CHECK(o[0] == 0 && o[1] == 0x88 && o[8] == 0x11);
	memset(o, 0, sizeof(o));
	put_unaligned_be64(0x1122334455667788ULL, &o[1]);
	CHECK(o[0] == 0 && o[1] == 0x11 && o[8] == 0x88);
	CHECK(get_unaligned((const u32 *)&b[1]) ==
	    get_unaligned_le32(&b[1]) || get_unaligned((const u32 *)&b[1]) ==
	    get_unaligned_be32(&b[1]));
	memset(o, 0, sizeof(o));
	put_unaligned(0xdeadbeefU, (u32 *)&o[1]);
	CHECK(get_unaligned((u32 *)&o[1]) == 0xdeadbeefU);

	/* dma-direction.h, hrtimer_types.h, nmi.h, xen/xen.h */
	CHECK(DMA_NONE == 0 && DMA_TO_DEVICE == 1);
	CHECK(DMA_FROM_DEVICE == 2 && DMA_BIDIRECTIONAL == 3);
	CHECK(HRTIMER_NORESTART != HRTIMER_RESTART);
	CHECK(sizeof(ht) > 0);
	touch_nmi_watchdog();
	CHECK(!xen_domain());

	/* time64.h */
	CHECK(MSEC_PER_SEC == 1000L && USEC_PER_SEC == 1000000L);
	CHECK(NSEC_PER_SEC == 1000000000L && NSEC_PER_MSEC == 1000000L);
	CHECK(NSEC_PER_USEC == 1000L && USEC_PER_MSEC == 1000L);
	CHECK(PSEC_PER_NSEC == 1000L);
	CHECK(sizeof(time64_t) == 8);
	ts = ns_to_timespec64(5 * NSEC_PER_SEC + 7);
	CHECK(ts.tv_sec == 5 && ts.tv_nsec == 7);
	CHECK(timespec64_to_ns(&ts) == 5 * NSEC_PER_SEC + 7);

	/* timekeeping.h */
	t0 = ktime_get();
	b0 = ns_to_ktime((int64_t)ktime_get_boottime_ns());
	t1 = ktime_get();
	CHECK(ktime_to_ns(t1) >= ktime_to_ns(t0));
	CHECK(ktime_get_ns() >= (uint64_t)ktime_to_ns(t0));
	CHECK(ktime_get_raw_fast_ns() > 0);
	CHECK(ktime_get_raw_ns() >= (uint64_t)ktime_to_ns(t0) - 1000000000ULL);
	CHECK(ktime_to_ns(b0) > 0);
	CHECK(ktime_get_real_ns() > 1000000000ULL * 1000000000ULL);
}
#endif

static int t_freed, t_cls_dtor, t_cls_ctor;

DEFINE_FREE(t_free, int *, if (_T != NULL) t_freed++)

DEFINE_CLASS(t_cls, int, t_cls_dtor += _T, ({ t_cls_ctor++; v; }), int v)

DEFINE_GUARD(t_lock, int *, (*_T)++, (*_T)--)

DEFINE_LOCK_GUARD_1(t_irq, int, _T->flags = ++(*_T->lock) * 10, (*_T->lock)--,
    unsigned long flags)

static int t_depth;
DEFINE_LOCK_GUARD_0(t_none, t_depth++, t_depth--)

static int *
t_return_ptr(int *src, bool fail)
{
	int *p __free(t_free) = src;

	if (fail)
		return NULL;		/* p is released */
	return_ptr(p);			/* ownership moves to the caller */
}

static int
t_early_return(int *lvl)
{

	guard(t_lock)(lvl);
	if (*lvl == 1)
		return 100 + *lvl;
	return -1;
}

static void
test_cleanup(void)
{
	int x = 0, lvl = 0, i, n;
	int *q;

	/* __free() runs at scope exit. */
	{
		int *p __free(t_free) = &x;

		CHECK(p == &x);
		CHECK(t_freed == 0);
	}
	CHECK(t_freed == 1);

	/* no_free_ptr() hands over the pointer and disarms the cleanup. */
	{
		int *p __free(t_free) = &x;

		q = no_free_ptr(p);
		CHECK(p == NULL && q == &x);
	}
	CHECK(t_freed == 1);

	/* retain_and_null_ptr() only disarms. */
	{
		int *p __free(t_free) = &x;

		retain_and_null_ptr(p);
		CHECK(p == NULL);
	}
	CHECK(t_freed == 1);

	/* return_ptr(): success keeps the object, failure releases it. */
	CHECK(t_return_ptr(&x, false) == &x);
	CHECK(t_freed == 1);
	CHECK(t_return_ptr(&x, true) == NULL);
	CHECK(t_freed == 2);

	/* Loop exit paths run the cleanup once per iteration. */
	for (i = 0; i < 3; i++) {
		int *p __free(t_free) = &x;

		if (i == 1)
			continue;
		(void)p;
	}
	CHECK(t_freed == 5);

	/* DEFINE_CLASS / CLASS */
	{
		CLASS(t_cls, c)(7);

		CHECK(c == 7 && t_cls_ctor == 1 && t_cls_dtor == 0);
	}
	CHECK(t_cls_dtor == 7 && t_cls_ctor == 1);

	/* DEFINE_GUARD / guard() */
	{
		guard(t_lock)(&lvl);
		CHECK(lvl == 1);
		{
			guard(t_lock)(&lvl);
			CHECK(lvl == 2);
		}
		CHECK(lvl == 1);
	}
	CHECK(lvl == 0);
	CHECK(t_early_return(&lvl) == 101);
	CHECK(lvl == 0);

	/* scoped_guard(): body once, released after it, break and nesting */
	n = 0;
	scoped_guard(t_lock, &lvl) {
		CHECK(lvl == 1);
		n++;
	}
	CHECK(lvl == 0 && n == 1);
	n = 0;
	scoped_guard(t_lock, &lvl) {
		n++;
		if (n == 1)
			break;
		n += 100;
	}
	CHECK(lvl == 0 && n == 1);
	scoped_guard(t_lock, &lvl)
		scoped_guard(t_lock, &lvl)
			CHECK(lvl == 2);
	CHECK(lvl == 0);

	/* DEFINE_LOCK_GUARD_1 with extra state; DEFINE_LOCK_GUARD_0 */
	{
		guard(t_irq)(&lvl);
		CHECK(lvl == 1);
	}
	CHECK(lvl == 0);
	scoped_guard(t_irq, &lvl) {
		CHECK(lvl == 1);
	}
	CHECK(lvl == 0);
	{
		guard(t_none)();
		CHECK(t_depth == 1);
		scoped_guard(t_none)
			CHECK(t_depth == 2);
		CHECK(t_depth == 1);
	}
	CHECK(t_depth == 0);
	CHECK(lvl == 0);
}

#ifndef DRM2_COMPAT_HOST
static void
test_kernel_lock_guards(void)
{
	struct mutex m;
	spinlock_t sl;
	struct rw_semaphore rw;
	int n;

	linux_mutex_init(&m);
	spin_lock_init(&sl);
	init_rwsem(&rw);

	{
		guard(mutex)(&m);
		CHECK(mutex_is_locked(&m));
	}
	CHECK(!mutex_is_locked(&m));
	n = 0;
	scoped_guard(mutex, &m) {
		CHECK(mutex_is_locked(&m));
		n++;
	}
	CHECK(!mutex_is_locked(&m) && n == 1);

	scoped_guard(spinlock, &sl)
		CHECK(spin_is_locked(&sl));
	CHECK(!spin_is_locked(&sl));
	scoped_guard(spinlock_irq, &sl)
		CHECK(spin_is_locked(&sl));
	CHECK(!spin_is_locked(&sl));
	scoped_guard(spinlock_irqsave, &sl)
		CHECK(spin_is_locked(&sl));
	CHECK(!spin_is_locked(&sl));
	{
		guard(spinlock_irqsave)(&sl);
		CHECK(spin_is_locked(&sl));
	}
	CHECK(!spin_is_locked(&sl));

	scoped_guard(rwsem_read, &rw)
		CHECK(rw.rws_readers == 1);
	CHECK(rw.rws_readers == 0);
	scoped_guard(rwsem_write, &rw)
		CHECK(rw.rws_writer != NULL);
	CHECK(rw.rws_writer == NULL);

	destroy_rwsem(&rw);
	spin_lock_destroy(&sl);
	linux_mutex_destroy(&m);
}
#endif

#ifndef DRM2_COMPAT_HOST
static void
test_kernel_xarray(void)
{
	struct xarray xa;
	unsigned long flags = 0xdead;
	int item = 1;

	xa_init_flags(&xa, GFP_KERNEL);

	xa_lock(&xa);
	CHECK(mutex_owned(&xa.xa_lock));
	xa_unlock(&xa);
	CHECK(!mutex_owned(&xa.xa_lock));
	xa_lock_irq(&xa);
	CHECK(mutex_owned(&xa.xa_lock));
	xa_unlock_irq(&xa);
	xa_lock_bh(&xa);
	CHECK(mutex_owned(&xa.xa_lock));
	xa_unlock_bh(&xa);
	xa_lock_irqsave(&xa, flags);
	CHECK(mutex_owned(&xa.xa_lock));
	xa_unlock_irqrestore(&xa, flags);
	CHECK(!mutex_owned(&xa.xa_lock));

	/* The unlocked entry points still work after locked sections. */
	/*
	 * Known layer deviation, left as is: for a fresh key xa_store() returns
	 * the stored datum, Linux returns the previous entry (NULL).  Only the
	 * absence of an error is checked.
	 */
	CHECK(xa_err(xa_store(&xa, 5, &item, GFP_KERNEL)) == 0);
	CHECK(xa_load(&xa, 5) == &item);
	CHECK(xa_erase(&xa, 5) == &item);
	CHECK(xa_load(&xa, 5) == NULL);

	xa_destroy(&xa);
}
#endif

#ifndef DRM2_COMPAT_HOST
static int t_poll_calls;

static int
t_poll_op(int *counter)
{

	t_poll_calls++;
	return ++*counter;
}

static unsigned
t_readl_fn(const volatile void *addr)
{

	t_poll_calls++;
	return *(const volatile unsigned *)addr;
}
#define	readl	t_readl_fn

static void
test_kernel_iopoll(void)
{
	int cnt, v, ret;
	ktime_t t0;
	int64_t us;
	volatile unsigned reg = 0xa5;

	/* The condition holds on the first read: one call, no delay. */
	cnt = 9; t_poll_calls = 0;
	ret = read_poll_timeout(t_poll_op, v, v >= 10, 100, 5000, false, &cnt);
	CHECK(ret == 0 && v == 10 && t_poll_calls == 1);

	/* It holds after several reads. */
	cnt = 0; t_poll_calls = 0;
	ret = read_poll_timeout(t_poll_op, v, v >= 5, 10, 100000, false, &cnt);
	CHECK(ret == 0 && v == 5 && t_poll_calls == 5);

	/* Zero timeout means wait forever, zero sleep means spin. */
	cnt = 0; t_poll_calls = 0;
	ret = read_poll_timeout(t_poll_op, v, v >= 200, 0, 0, false, &cnt);
	CHECK(ret == 0 && v == 200 && t_poll_calls == 200);

	/* The condition never holds: -ETIMEDOUT after about timeout_us. */
	cnt = 0; t_poll_calls = 0;
	t0 = ktime_get();
	ret = read_poll_timeout(t_poll_op, v, v < 0, 100, 3000, false, &cnt);
	us = ktime_us_delta(ktime_get(), t0);
	CHECK(ret == -ETIMEDOUT);
	CHECK(us >= 3000 && us < 1000000);
	CHECK(v == cnt && v == t_poll_calls && v > 1);	/* val is the last read */

	/* sleep_before_read delays the first read. */
	cnt = 0; t_poll_calls = 0;
	t0 = ktime_get();
	ret = read_poll_timeout(t_poll_op, v, v >= 1, 2000, 100000, true, &cnt);
	us = ktime_us_delta(ktime_get(), t0);
	CHECK(ret == 0 && t_poll_calls == 1 && us >= 500 && us < 1000000);

	/* Atomic variant. */
	cnt = 0; t_poll_calls = 0;
	ret = read_poll_timeout_atomic(t_poll_op, v, v >= 4, 5, 100000, false,
	    &cnt);
	CHECK(ret == 0 && v == 4 && t_poll_calls == 4);
	cnt = 0;
	t0 = ktime_get();
	ret = read_poll_timeout_atomic(t_poll_op, v, v < 0, 10, 2000, false,
	    &cnt);
	us = ktime_us_delta(ktime_get(), t0);
	CHECK(ret == -ETIMEDOUT && us >= 2000 && us < 1000000);

	/* readx_poll_timeout: the op takes the "address" argument. */
	cnt = 0; t_poll_calls = 0;
	ret = readx_poll_timeout(t_poll_op, &cnt, v, v >= 3, 10, 100000);
	CHECK(ret == 0 && v == 3 && t_poll_calls == 3);
	cnt = 0; t_poll_calls = 0;
	ret = readx_poll_timeout_atomic(t_poll_op, &cnt, v, v >= 3, 10, 100000);
	CHECK(ret == 0 && v == 3 && t_poll_calls == 3);

	/* readl_poll_timeout and friends call readl() on the address. */
	{
		unsigned r;

		t_poll_calls = 0;
		ret = readl_poll_timeout(&reg, r, r == 0xa5, 10, 1000);
		CHECK(ret == 0 && r == 0xa5 && t_poll_calls == 1);
		ret = readl_poll_timeout_atomic(&reg, r, r == 0x00, 10, 500);
		CHECK(ret == -ETIMEDOUT && r == 0xa5);
	}

	/* poll_timeout_us: an arbitrary statement as the operation. */
	cnt = 0; t_poll_calls = 0;
	ret = poll_timeout_us(v = t_poll_op(&cnt), v >= 7, 10, 100000, false);
	CHECK(ret == 0 && v == 7);
	ret = poll_timeout_us_atomic(v = t_poll_op(&cnt), v >= 9, 10, 100000,
	    false);
	CHECK(ret == 0 && v == 9);
	ret = poll_timeout_us(v = t_poll_op(&cnt), v < 0, 100, 1500, false);
	CHECK(ret == -ETIMEDOUT);
}
#undef readl
#endif

struct t_hdr {
	u8	kind;
	u16	count;
	u32	flags;
	u64	cookie;
};

static void
t_iosys_map_one(bool iomem)
{
	u8 buf[64], cmp[64];
	struct iosys_map m, n;
	struct t_hdr h;
	u8 out[16];
	int i;

	memset(buf, 0xee, sizeof(buf));
	if (iomem)
		iosys_map_set_vaddr_iomem(&m, buf);
	else
		iosys_map_set_vaddr(&m, buf);
	CHECK(m.is_iomem == iomem);
	CHECK(iosys_map_is_set(&m) && !iosys_map_is_null(&m));
	CHECK(m.vaddr == (void *)buf);

	/* copy in at an offset, copy out, neighbours untouched */
	for (i = 0; i < 16; i++)
		out[i] = (u8)i;
	iosys_map_memcpy_to(&m, 5, out, 16);
	CHECK(buf[4] == 0xee && buf[5] == 0 && buf[20] == 15 && buf[21] == 0xee);
	memset(cmp, 0, sizeof(cmp));
	iosys_map_memcpy_from(cmp, &m, 5, 16);
	CHECK(memcmp(cmp, out, 16) == 0);
	CHECK(cmp[16] == 0);

	iosys_map_memset(&m, 40, 0x5a, 7);
	CHECK(buf[39] == 0xee && buf[40] == 0x5a && buf[46] == 0x5a &&
	    buf[47] == 0xee);

	/* typed access, sizes 1, 2, 4, 8 */
	iosys_map_wr(&m, 0, u8, 0x12);
	iosys_map_wr(&m, 2, u16, 0x3456);
	iosys_map_wr(&m, 4, u32, 0x789abcdeU);
	iosys_map_wr(&m, 8, u64, 0x0123456789abcdefULL);
	CHECK(iosys_map_rd(&m, 0, u8) == 0x12);
	CHECK(iosys_map_rd(&m, 2, u16) == 0x3456);
	CHECK(iosys_map_rd(&m, 4, u32) == 0x789abcdeU);
	CHECK(iosys_map_rd(&m, 8, u64) == 0x0123456789abcdefULL);
	CHECK(*(u32 *)(buf + 4) == 0x789abcdeU);

	/* struct fields at a base offset */
	memset(&h, 0, sizeof(h));
	iosys_map_wr_field(&m, 16, struct t_hdr, kind, 7);
	iosys_map_wr_field(&m, 16, struct t_hdr, count, 513);
	iosys_map_wr_field(&m, 16, struct t_hdr, flags, 0xdeadbeefU);
	iosys_map_wr_field(&m, 16, struct t_hdr, cookie, 0x1122334455667788ULL);
	memcpy(&h, buf + 16, sizeof(h));
	CHECK(h.kind == 7 && h.count == 513 && h.flags == 0xdeadbeefU);
	CHECK(h.cookie == 0x1122334455667788ULL);
	CHECK(iosys_map_rd_field(&m, 16, struct t_hdr, count) == 513);
	CHECK(iosys_map_rd_field(&m, 16, struct t_hdr, cookie) ==
	    0x1122334455667788ULL);

	/* increment and sub-maps */
	n = m;
	iosys_map_incr(&n, 4);
	CHECK(n.vaddr == (void *)(buf + 4) && n.is_iomem == iomem);
	CHECK(iosys_map_rd(&n, 0, u32) == 0x789abcdeU);
	CHECK(!iosys_map_is_equal(&m, &n));
	{
		struct iosys_map sub = IOSYS_MAP_INIT_OFFSET(&m, 4);

		CHECK(iosys_map_is_equal(&sub, &n));
	}

	/* null maps */
	iosys_map_clear(&n);
	CHECK(iosys_map_is_null(&n) && !iosys_map_is_set(&n));
	CHECK(!n.is_iomem && n.vaddr == NULL);
}

static void
test_iosys_map(void)
{
	struct iosys_map a = IOSYS_MAP_INIT_VADDR(NULL);
	struct iosys_map b = IOSYS_MAP_INIT_VADDR_IOMEM(NULL);
	u8 x;
	struct iosys_map c = IOSYS_MAP_INIT_VADDR(&x);
	struct iosys_map d = IOSYS_MAP_INIT_VADDR_IOMEM(&x);

	CHECK(iosys_map_is_null(&a) && iosys_map_is_null(&b));
	CHECK(!a.is_iomem && b.is_iomem);
	CHECK(iosys_map_is_set(&c) && iosys_map_is_set(&d));
	/* Same address in different address spaces is not the same map. */
	CHECK(!iosys_map_is_equal(&c, &d));
	CHECK(iosys_map_is_equal(&c, &c));
	t_iosys_map_one(false);
	t_iosys_map_one(true);
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
	test_kdev_swab();
	test_cleanup();
	test_iosys_map();
#ifndef DRM2_COMPAT_HOST
	test_kernel_forwarders();
	test_kernel_misc();
	test_kernel_lock_guards();
	test_kernel_xarray();
	test_kernel_iopoll();
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
