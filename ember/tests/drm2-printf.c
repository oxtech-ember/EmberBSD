/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the Linux style formatting of the 6.18 core
 * (drm618/drm618_printf.c): %pV (a nested format with its arguments), %ps and
 * %pS (a code address), and the ordinary conversions around them, which is
 * what drm_print.c of Linux 6.18 hands to its snprintf and printk.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/printk.h>
#include <linux/stdarg.h>

int	drm618_snprintf(char *, size_t, const char *, ...) __printflike(3, 4);
int	drm618_vsnprintf(char *, size_t, const char *, va_list);

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static int
t_inner(char *buf, size_t len, const char *fmt, ...)
{
	struct va_format vaf;
	va_list ap;
	int n;

	va_start(ap, fmt);
	vaf.fmt = fmt;
	vaf.va = &ap;
	n = drm618_snprintf(buf, len, "[drm] %pV|", &vaf);
	va_end(ap);
	return n;
}

int	drm2_printf_run(void);

int
drm2_printf_run(void)
{
	char buf[128];
	int n;

	t_run = t_fail = 0;
	n = drm618_snprintf(buf, sizeof buf, "plain %d %s %5u|%-4x|%c", -7, "str", 42u, 0xab, 'z');
	CHECK(n == (int)strlen(buf) && strcmp(buf, "plain -7 str    42|ab  |z") == 0);
	n = drm618_snprintf(buf, sizeof buf, "%lu %llu %zu %ld", 1ul << 40, 1ull << 41, (size_t)99, -5l);
	CHECK(strcmp(buf, "1099511627776 2199023255552 99 -5") == 0);
	/* a nested format takes its own arguments and leaves the outer ones in order */
	n = t_inner(buf, sizeof buf, "flip %d of %s", 3, "crtc");
	CHECK(strcmp(buf, "[drm] flip 3 of crtc|") == 0 && n == (int)strlen(buf));
	n = drm618_snprintf(buf, sizeof buf, "%s:%pV;%d", "a", &(struct va_format){ 0 }, 9);
	CHECK(n > 0);			/* an empty va_format is not followed: no crash, nothing assumed */
	/* a code address is printed as hex, and the text around it is kept */
	n = drm618_snprintf(buf, sizeof buf, "[drm:%ps] x", (void *)0x1234);
	CHECK(strncmp(buf, "[drm:", 5) == 0 && strstr(buf, "1234") != NULL &&
	    strcmp(buf + n - 3, "] x") == 0);
	/* the count of the full text, with a buffer that is too small */
	n = drm618_snprintf(buf, 8, "%s", "0123456789");
	CHECK(n == 10 && strcmp(buf, "0123456") == 0);
	n = drm618_snprintf(NULL, 0, "%d%s", 12345, "xy");
	CHECK(n == 7);
	n = drm618_snprintf(buf, sizeof buf, "100%%");
	CHECK(strcmp(buf, "100%") == 0);
	n = drm618_snprintf(buf, sizeof buf, "%*d|%.3s", 5, 42, "abcdef");
	CHECK(strcmp(buf, "   42|abc") == 0);
	if (t_fail != 0)
		printf("drm2-printf: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-printf: %d checks, 0 failed\n", t_run);
	return t_fail;
}
