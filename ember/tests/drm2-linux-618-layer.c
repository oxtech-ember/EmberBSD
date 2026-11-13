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

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/property.h>
#include <linux/units.h>
#include <linux/xarray.h>
#include <linux/hrtimer.h>
#include <linux/random.h>
#include <linux/sched.h>
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

static int dorder[8];
static unsigned ndorder;

static void
dact(void *p)
{
	if (ndorder < 8)
		dorder[ndorder] = *(int *)p;
	ndorder++;
}

static void
test_devres(void)
{
	static char da, db;
	struct device *d1 = (struct device *)&da, *d2 = (struct device *)&db;
	int a = 1, b = 2, c = 3, e = 4;
	char *m;

	ndorder = 0;
	CHECK(devm_add_action(d1, dact, &a) == 0);
	CHECK(devm_add_action(d2, dact, &b) == 0);
	CHECK(devm_add_action_or_reset(d1, dact, &c) == 0);
	CHECK(devm_add_action(d1, dact, &e) == 0);
	m = devm_kzalloc(d1, 50, GFP_KERNEL);
	CHECK(m != NULL && m[0] == 0 && m[49] == 0);

	/* One action runs now; the others stay. */
	devm_release_action(d1, dact, &c);
	CHECK(ndorder == 1 && dorder[0] == 3);
	/* Unknown action: nothing happens. */
	devm_release_action(d1, dact, &b);
	CHECK(ndorder == 1);
	devm_kfree(d1, NULL);

	/* Everything of d1, newest first; d2's stays. */
	devres_release_all(d1);
	CHECK(ndorder == 3 && dorder[1] == 4 && dorder[2] == 1);
	devres_release_all(d1);
	CHECK(ndorder == 3);
	devres_release_all(d2);
	CHECK(ndorder == 4 && dorder[3] == 2);

	/* Memory freed early is not freed again. */
	m = devm_kmalloc(d2, 16, GFP_KERNEL);
	CHECK(m != NULL);
	devm_kfree(d2, m);
	devres_release_all(d2);
	CHECK(ndorder == 4);
}

static void
test_misc(void)
{
	struct xarray xa;
	struct xa_limit lim = XA_LIMIT(5, 7);
	uint32_t id;
	static int v;
	char *p = (char *)0;

	CHECK(lim.min == 5 && lim.max == 7);
	xa_init_flags(&xa, XA_FLAGS_ALLOC);
	CHECK(xa_empty(&xa));
	CHECK(xa_alloc(&xa, &id, &v, lim, GFP_KERNEL) == 0 && id == 5);
	CHECK(!xa_empty(&xa));
	CHECK(xa_alloc(&xa, &id, &v, lim, GFP_KERNEL) == 0 && id == 6);
	CHECK(xa_alloc(&xa, &id, &v, lim, GFP_KERNEL) == 0 && id == 7);
	CHECK(xa_alloc(&xa, &id, &v, lim, GFP_KERNEL) != 0);
	CHECK(xa_is_err(XA_ERROR(-ENOMEM)) && !xa_is_err(&v) && !xa_is_err(NULL));
	xa_destroy(&xa);

	CHECK(FIELD_PREP(0xf0, 0x3) == 0x30);
	CHECK(FIELD_PREP(0xff00, 0x12) == 0x1200);
	CHECK(FIELD_PREP(0x0f, 0x1f) == 0x0f);
	CHECK(ffz(0x0fUL) == 4 && ffz(0UL) == 0 && ffz(0x7UL) == 3);
	CHECK(KHZ2PICOS(148500) == 6734);	/* 148.5 MHz: 6.734 ns */
	(void)p;

	/* No pci_dev for a device nobody made one for; no firmware node. */
	CHECK(to_pci_dev((struct device *)&v) == NULL);
	CHECK(dev_fwnode((struct device *)&v) == NULL);
	CHECK(!device_property_present((struct device *)&v, "x"));
	CHECK(dev_err_probe((struct device *)NULL, -EPROBE_DEFER, "quiet\n") == -EPROBE_DEFER);
}

static void
test_task(void)
{
	struct task_struct *t = current;
	struct pid *pid = task_tgid(t);

	/* comm is the process's name, p_comm */
	CHECK(strcmp(t->comm, curproc->p_comm) == 0);
	CHECK(task_pid_vnr(t) == curproc->p_pid && task_tgid_nr(t) == curproc->p_pid);
	CHECK(pid_nr(pid) == curproc->p_pid);
	CHECK(get_pid(pid) == pid);
	put_pid(pid);
	CHECK(pid_task(pid, PIDTYPE_TGID) == t);
	/* another process's pid does not name a task here */
	CHECK(pid_task((struct pid *)(uintptr_t)(curproc->p_pid + 1000), PIDTYPE_TGID) == NULL);
	CHECK(pid_task(NULL, PIDTYPE_TGID) == NULL);
	CHECK(overflowuid == 65534);
	CHECK(t->pid == curproc->p_pid && t->tgid == curproc->p_pid);
	CHECK(task_pid_nr(t) == curproc->p_pid);
}

static void
test_strscpy(void)
{
	char d[8];
	char big[4];

	/* both forms: with the size, and without (the size of the array) */
	CHECK(strscpy(d, "abc", sizeof(d)) == 3 && strcmp(d, "abc") == 0);
	CHECK(strscpy(d, "xyz") == 3 && strcmp(d, "xyz") == 0);
	CHECK(strscpy(d, "0123456789") == -E2BIG && strcmp(d, "0123456") == 0);
	CHECK(strscpy(big, "0123456789") == -E2BIG && strcmp(big, "012") == 0);
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
	test_devres();
	test_misc();
	test_task();
	test_strscpy();
	printf("drm2-linux-618-layer: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
