/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the parts of the Linux layer that the i915 driver of
 * Linux 6.18 needs and that have behaviour of their own: fixed width bit masks,
 * FIELD_FIT, overflows_type, MMIO accessors, the seqcount with a mutex, the
 * wait queue entries, the chain of a scatterlist, the tasklet bits,
 * Ascii85, msleep_interruptible and the trace points that do nothing.
 * Run inside drmkms618_test.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>

#include <linux/ascii85.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/timekeeping.h>
#include <linux/mutex.h>
#include <linux/overflow.h>
#include <linux/pm_runtime.h>
#include <linux/refcount.h>
#include <linux/scatterlist.h>
#include <linux/seqlock.h>
#include <linux/stddef.h>
#include <linux/wait.h>
#include <asm/hypervisor.h>
#include <xen/xen.h>

#include "i915_trace.h"
#include "intel_display_trace.h"
#include "intel_uncore_trace.h"

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

/* ---- wait queue entries ---- */
struct t_ent {
	wait_queue_entry_t	e;
	int			calls;
	int			order;
	int			ret;
	void			*key;
};
static int t_order;

static int
t_wake(wait_queue_entry_t *e, unsigned int mode __unused, int flags __unused,
    void *key)
{
	struct t_ent *t = container_of(e, struct t_ent, e);

	t->calls++;
	t->order = ++t_order;
	t->key = key;
	return t->ret;
}

static void
t_ent_init(struct t_ent *t, int ret)
{

	memset(t, 0, sizeof(*t));
	init_waitqueue_func_entry(&t->e, t_wake);
	INIT_LIST_HEAD(&t->e.entry);
	t->ret = ret;
}

struct t_s { int a; char b[7]; };

int	drm2_i915_layer_run(void);

int
drm2_i915_layer_run(void)
{
	uint64_t buf64[2];
	uint8_t *b = (uint8_t *)buf64;
	wait_queue_head_t wq;
	struct t_ent e1, e2, e3;
	struct scatterlist a[3], c[2], *sg;
	struct tasklet_struct tl;
	seqcount_mutex_t sc;
	struct mutex mtx;
	refcount_t rc;
	char out[ASCII85_BUFSZ];
	unsigned int n, sq, side = 0;
	long sum;
	int xside = 0;
	uint64_t t0, t1;
	int x = -1;
	long y = 5;

	t_run = t_fail = 0;

	/* fixed width masks */
	CHECK(BIT_U8(7) == 0x80 && BIT_U16(15) == 0x8000);
	CHECK(BIT_U32(31) == 0x80000000u && BIT_U64(63) == 0x8000000000000000ull);
	CHECK(GENMASK_U8(6, 3) == 0x78 && GENMASK_U8(7, 0) == 0xff);
	CHECK(GENMASK_U16(15, 0) == 0xffff && GENMASK_U16(11, 4) == 0x0ff0);
	CHECK(GENMASK_U32(31, 31) == 0x80000000u && GENMASK_U32(31, 0) == 0xffffffffu);
	CHECK(GENMASK_U32(7, 4) == 0xf0 && GENMASK_U32(0, 0) == 1);
	CHECK(GENMASK_U64(63, 32) == 0xffffffff00000000ull && GENMASK_U64(0, 0) == 1);
	CHECK(GENMASK_U64(63, 0) == ~0ull);

	/* FIELD_FIT */
	CHECK(FIELD_FIT(0xf0u, 0xf));
	CHECK(!FIELD_FIT(0xf0u, 0x10));
	CHECK(FIELD_FIT(GENMASK_U32(23, 16), 0xff) && !FIELD_FIT(GENMASK_U32(23, 16), 0x100));

	/* overflows_type, castable_to_type, typeof_member */
	CHECK(!overflows_type(255, uint8_t) && overflows_type(256, uint8_t));
	CHECK(overflows_type(-1, uint8_t) && overflows_type(x, unsigned int));
	CHECK(!overflows_type(0xffffffffull, uint32_t) && overflows_type(0x100000000ull, uint32_t));
	CHECK(!overflows_type(y, int) && !overflows_type(y, y));
	CHECK(castable_to_type(10, uint8_t) && !castable_to_type(300, uint8_t));
	CHECK(castable_to_type(x, int) && !castable_to_type(y, int));
	CHECK(sizeof(typeof_member(struct t_s, a)) == sizeof(int));
	CHECK(sizeof(typeof_member(struct t_s, b)) == 7);

	/* MMIO accessors: little endian, the width asked for */
	memset(buf64, 0, sizeof(buf64));
	writel(0x11223344u, b);
	CHECK(b[0] == 0x44 && b[1] == 0x33 && b[2] == 0x22 && b[3] == 0x11);
	CHECK(readw(b) == 0x3344 && readw(b + 2) == 0x1122 && readb(b + 1) == 0x33);
	writew(0xaabb, b + 4);
	writeb(0xcc, b + 6);
	CHECK(b[4] == 0xbb && b[5] == 0xaa && b[6] == 0xcc && b[7] == 0);
	writeq(0x0102030405060708ull, b + 8);
	CHECK(b[8] == 8 && b[15] == 1 && readq(b + 8) == 0x0102030405060708ull);
	CHECK(readl(b + 8) == 0x05060708u);

	/* refcount_read */
	refcount_set(&rc, 3);
	CHECK(refcount_read(&rc) == 3);
	refcount_inc(&rc);
	CHECK(refcount_read(&rc) == 4);

	/* the seqcount with a mutex */
	mutex_init(&mtx);
	seqcount_mutex_init(&sc, &mtx);
	sq = read_seqcount_begin(&sc);
	CHECK(!read_seqcount_retry(&sc, sq));
	mutex_lock(&mtx);
	write_seqcount_begin(&sc);
	CHECK(read_seqcount_retry(&sc, sq));
	write_seqcount_end(&sc);
	mutex_unlock(&mtx);
	CHECK(read_seqcount_retry(&sc, sq));
	CHECK(seqprop_sequence(&sc) == sq + 2);
	sq = read_seqcount_begin(&sc);
	write_seqcount_invalidate(&sc);
	CHECK(read_seqcount_retry(&sc, sq) && seqprop_sequence(&sc) == sq + 2);
	mutex_destroy(&mtx);

	/* wait queue entries */
	init_waitqueue_head(&wq);
	t_ent_init(&e1, 1);
	t_ent_init(&e2, 1);
	t_ent_init(&e3, 1);
	spin_lock(&wq.lock);
	__add_wait_queue_entry_tail(&wq, &e1.e);
	__add_wait_queue_entry_tail(&wq, &e2.e);
	__add_wait_queue_entry_tail(&wq, &e3.e);
	spin_unlock(&wq.lock);
	t_order = 0;
	__wake_up(&wq, 0, 0, &side);
	CHECK(e1.calls == 1 && e2.calls == 1 && e3.calls == 1);
	CHECK(e1.order == 1 && e2.order == 2 && e3.order == 3);
	CHECK(e1.key == &side && e3.key == &side);
	/* wake_up wakes the first exclusive entry and the ones before it */
	e2.e.flags |= WQ_FLAG_EXCLUSIVE;
	e3.e.flags |= WQ_FLAG_EXCLUSIVE;
	wake_up(&wq);
	CHECK(e1.calls == 2 && e2.calls == 2 && e3.calls == 1);
	wake_up_all(&wq);
	CHECK(e1.calls == 3 && e2.calls == 3 && e3.calls == 2);
	/* a function that says no does not use up the exclusive count */
	e2.ret = 0;
	wake_up(&wq);
	CHECK(e2.calls == 4 && e3.calls == 3);
	/* a negative answer ends the walk */
	e1.ret = -1;
	wake_up_all(&wq);
	CHECK(e1.calls == 5 && e2.calls == 4 && e3.calls == 3);
	remove_wait_queue(&wq, &e1.e);
	remove_wait_queue(&wq, &e2.e);
	remove_wait_queue(&wq, &e3.e);
	CHECK(list_empty(&wq.head));
	wake_up_all(&wq);
	CHECK(e1.calls == 5);
	/* add_wait_queue puts a plain entry in front; autoremove takes it off */
	{
		DEFINE_WAIT(w);

		t_ent_init(&e1, 1);
		add_wait_queue(&wq, &e1.e);
		add_wait_queue(&wq, &w);
		CHECK(wq.head.next == &w.entry && !list_empty(&w.entry));
		wake_up_all(&wq);
		CHECK(list_empty(&w.entry) && e1.calls == 1);
		remove_wait_queue(&wq, &e1.e);
	}
	destroy_waitqueue_head(&wq);

	/* the chain of a scatterlist */
	memset(a, 0, sizeof(a));
	memset(c, 0, sizeof(c));
	sg_init_table(a, 3);
	sg_init_table(c, 2);
	a[0].length = 1;
	a[1].length = 2;
	c[0].length = 3;
	c[1].length = 4;
	sg_chain(a, 3, c);
	CHECK(sg_is_chain(&a[2]) && !sg_is_chain(&a[1]) && !sg_is_last(&a[2]));
	CHECK(sg_chain_ptr(&a[2]) == c && sg_is_last(&c[1]));
	n = 0;
	sum = 0;
	for (sg = a; sg != NULL; sg = sg_next(sg)) {
		n++;
		sum += sg->length;
	}
	CHECK(n == 4 && sum == 10);
	CHECK(sg_next(&a[1]) == &c[0] && sg_next(&c[1]) == NULL);

	/* the tasklet bits */
	memset(&tl, 0, sizeof(tl));
	CHECK(TASKLET_STATE_SCHED == 0 && TASKLET_STATE_RUN == 1);
	CHECK(tasklet_trylock(&tl) && !tasklet_trylock(&tl));
	CHECK((tl.state & (1ul << TASKLET_STATE_RUN)) != 0);
	tasklet_unlock(&tl);
	CHECK(tl.state == 0 && tasklet_trylock(&tl));
	tasklet_unlock(&tl);
	tasklet_unlock_spin_wait(&tl);
	CHECK(atomic_read(&tl.count) == 0);
	tasklet_disable_nosync(&tl);
	tasklet_disable_nosync(&tl);
	CHECK(atomic_read(&tl.count) == 2);
	tasklet_enable(&tl);
	tasklet_enable(&tl);
	CHECK(atomic_read(&tl.count) == 0);
	{
		struct { int pad; struct tasklet_struct tasklet; } holder, *h;

		h = from_tasklet(h, &holder.tasklet, tasklet);
		CHECK(h == &holder);
	}

	/* Ascii85 */
	CHECK(strcmp(ascii85_encode(0, out), "z") == 0);
	CHECK(strcmp(ascii85_encode(0x4d616e20u, out), "9jqo^") == 0);	/* "Man " */
	CHECK(strcmp(ascii85_encode(0xffffffffu, out), "s8W-!") == 0);
	CHECK(strcmp(ascii85_encode(1, out), "!!!!\"") == 0);
	CHECK(ascii85_encode_len(0) == 0 && ascii85_encode_len(1) == 1);
	CHECK(ascii85_encode_len(4) == 1 && ascii85_encode_len(5) == 2);

	/* a sleep that is not interrupted says nothing is left */
	t0 = ktime_get_raw_fast_ns();
	CHECK(msleep_interruptible(5) == 0);
	t1 = ktime_get_raw_fast_ns();
	CHECK(t1 >= t0);

	/* host state */
	CHECK(hypervisor_is_type(X86_HYPER_NATIVE) == (vm_guest == VM_GUEST_NO));
	CHECK(!hypervisor_is_type(X86_HYPER_KVM) && !xen_pv_domain() && !xen_domain());
	CHECK(!pm_runtime_suspended(NULL));

	/* trace points: arguments type checked, never evaluated, never enabled */
	trace_i915_request_add(xside++);
	trace_intel_pipe_enable(xside++, xside++);
	trace_i915_reg_rw(xside++);
	CHECK(xside == 0);
	CHECK(!trace_i915_request_add_enabled() && !trace_intel_pipe_enable_enabled());

	if (t_fail != 0)
		printf("drm2-i915-layer: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-i915-layer: %d checks, 0 failed\n", t_run);
	return t_fail;
}
