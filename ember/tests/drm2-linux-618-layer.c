/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the layer pieces added for the 6.18 DRM core: seq_file (fixed buffer), seq_buf's DECLARE_SEQ_BUF, guid_t
 * helpers, hrtimer_setup / hrtimer_try_to_cancel / node.expires, rwlock
 * write_lock.  Run by sys/modules/drmkms_linux_test; the file includes the
 * prefix header like the files of the core.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <drm618_prefix.h>

#include <linux/errno.h>
#include <linux/hrtimer.h>
#include <linux/random.h>
#include <linux/seq_buf.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/uuid.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static void
test_seq_file(void)
{
	struct seq_file m;
	char buf[24];

	seq_file_init(&m, buf, sizeof(buf));
	seq_puts(&m, "ab");
	seq_putc(&m, 'c');
	seq_printf(&m, "-%d-%s", 42, "x");
	CHECK(m.count == 8 && memcmp(buf, "abc-42-x", 8) == 0);
	CHECK(!seq_has_overflowed(&m));
	CHECK(seq_write(&m, "1234", 4) == 0 && m.count == 12);

	/* Output that does not fit is dropped as a whole and marked. */
	seq_printf(&m, "%020d", 7);
	CHECK(seq_has_overflowed(&m));
	CHECK(m.count == m.size);
	seq_puts(&m, "more");
	CHECK(m.count == m.size && memcmp(buf, "abc-42-x1234", 12) == 0);

	/* A buffer of exactly the size is full, not an error before it. */
	seq_file_init(&m, buf, 4);
	seq_puts(&m, "abc");
	CHECK(!seq_has_overflowed(&m) && m.count == 3);
	seq_putc(&m, 'd');
	CHECK(seq_has_overflowed(&m));
}

static void
test_seq_buf(void)
{
	DECLARE_SEQ_BUF(sb, 16);

	CHECK(!seq_buf_has_overflowed(&sb));
	seq_buf_printf(&sb, "year %d", 2026);
	CHECK(strcmp(seq_buf_str(&sb), "year 2026") == 0);
	seq_buf_printf(&sb, " week %d of %d", 45, 52);
	CHECK(seq_buf_has_overflowed(&sb));
}

static void
test_guid(void)
{
	guid_t a, b, c;
	static const unsigned char raw[16] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
	unsigned char out[16];

	CHECK(sizeof(guid_t) == UUID_SIZE);
	memset(&a, 0, sizeof(a));
	CHECK(guid_is_null(&a));
	import_guid(&a, raw);
	CHECK(!guid_is_null(&a));
	export_guid(out, &a);
	CHECK(memcmp(out, raw, 16) == 0);
	guid_copy(&b, &a);
	CHECK(guid_equal(&a, &b));
	b.guid_bytes[15]++;
	CHECK(!guid_equal(&a, &b));

	guid_gen(&b);
	guid_gen(&c);
	CHECK(!guid_equal(&b, &c));
	CHECK((b.guid_bytes[7] & 0xf0) == 0x40 && (b.guid_bytes[8] & 0xc0) == 0x80);
	CHECK(!guid_is_null(&b));
}

static unsigned fired;

static enum hrtimer_restart
timer_fn(struct hrtimer *t)
{
	fired++;
	return HRTIMER_NORESTART;
}

static void
test_hrtimer(void)
{
	struct hrtimer t;
	ktime_t e = ns_to_ktime(5000000000LL);

	hrtimer_setup(&t, timer_fn, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	CHECK(t.function == timer_fn);
	CHECK(!hrtimer_active(&t));
	/* Not active: nothing to cancel. */
	CHECK(hrtimer_try_to_cancel(&t) == 0);

	hrtimer_start(&t, e, HRTIMER_MODE_REL);
	CHECK(hrtimer_active(&t));
	/* node.expires is hrt_expires, under the name 6.18 code uses. */
	CHECK(t.node.expires == t.hrt_expires && t.node.expires == e);
	/* Pending: cancelled, and not run. */
	CHECK(hrtimer_try_to_cancel(&t) == 1);
	CHECK(!hrtimer_active(&t));
	CHECK(fired == 0);
	CHECK(hrtimer_try_to_cancel(&t) == 0);

	/* A short one does run, and the cancel afterwards finds it idle. */
	hrtimer_start(&t, ns_to_ktime(5000000), HRTIMER_MODE_REL);
	kpause("hrtmr", false, mstohz(60), NULL);
	CHECK(fired == 1);
	CHECK(hrtimer_try_to_cancel(&t) == 0);
	hrtimer_cancel(&t);
}

static void
test_rwlock(void)
{
	rwlock_t rw;

	rwlock_init(&rw);
	write_lock(&rw);
	write_unlock(&rw);
	read_lock(&rw);
	read_lock(&rw);
	read_unlock(&rw);
	read_unlock(&rw);
	write_lock(&rw);
	write_unlock(&rw);
	rwlock_destroy(&rw);
	CHECK(1);
}

int	drm2_linux_618_layer_run(void);

int
drm2_linux_618_layer_run(void)
{

	t_run = t_fail = 0;
	test_seq_file();
	test_seq_buf();
	test_guid();
	test_hrtimer();
	test_rwlock();
	printf("drm2-linux-618-layer: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
