/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of the tested names in the MIT files of dist618/drm/i915, the
 * documented behaviour of the Linux interfaces, and the layer headers under test; the GPL original was not read.
 *
 * Kernel-side checks of the parts of the Linux layer that the second batch of
 * i915 files of Linux 6.18 needs and that have behaviour of their own: the
 * sleeping protocol with wait queue entries (prepare_to_wait, finish_wait,
 * schedule, wait on a variable), 64 bit compare and swap, castable_to_type
 * with the layer pgoff_t.  Run inside drmkms618_test after drm2-i915-layer.c.
 */

#include <sys/cdefs.h>
#include <sys/callout.h>
#include <sys/intr.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/kthread.h>
#include <sys/systm.h>

#include <asm/cpufeature.h>
#include <linux/dmi.h>
#include <linux/hdmi.h>
#include <linux/interrupt.h>
#include <linux/overflow.h>
#include <drm/drm_mipi_dsi.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/timer.h>
#include <linux/shmem_fs.h>
#include <linux/slab.h>
#include <linux/xarray.h>
#include <linux/shrinker.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <i915drm618_compat.h>
#include <i915drm618_dev.h>

/* a Linux interrupt handler takes the number and the cookie */
static unsigned long t_cnt(struct shrinker *s __unused, struct shrink_control *c __unused) { return 0; }
static int t_pset(const char *v __unused, const struct kernel_param *kp __unused) { return 0; }
static int t_pget(char *b __unused, const struct kernel_param *kp __unused) { return 0; }
static const struct kernel_param_ops t_pops = { .set = t_pset, .get = t_pget };
module_param_cb_unsafe(t618, &t_pops, NULL, 0600);
static_assert(__builtin_types_compatible_p(irq_handler_t, irqreturn_t (*)(int, void *)));

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

/* the wait on a variable of the tests uses a queue of its own */
static wait_queue_head_t t_vq;
#undef	__var_waitqueue
#define	__var_waitqueue(var)	(&t_vq)

static wait_queue_head_t t_q;
static volatile int t_up, t_flag, t_done;
static volatile long t_left;
static int t_var;
static volatile long t_vret;

/* a kernel thread that waits for t_flag the way Linux does: prepare_to_wait, test, schedule */
static void
t_waiter(void *cookie __unused)
{
	DEFINE_WAIT(w);

	t_up = 1;
	for (;;) {
		prepare_to_wait(&t_q, &w, TASK_UNINTERRUPTIBLE);
		if (t_flag)
			break;
		t_left = schedule_timeout(10 * hz);
	}
	finish_wait(&t_q, &w);
	t_done = 1;
	kthread_exit(0);
}

/* the same with ___wait_var_event on t_var */
static void
t_var_waiter(void *cookie __unused)
{

	t_up = 1;
	t_vret = ___wait_var_event(&t_var, t_var != 0, TASK_UNINTERRUPTIBLE, 0, 0,
	    t_left = schedule_timeout(10 * hz));
	t_done = 1;
	kthread_exit(0);
}

static int
t_start(void (*fn)(void *), const char *name, struct lwp **lp)
{

	t_up = t_flag = t_done = 0;
	t_left = 0;
	if (kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN, NULL,
	    fn, NULL, lp, "%s", name) != 0)
		return -1;
	while (!t_up)
		kpause("t618w", false, 1, NULL);
	kpause("t618w", false, hz / 20 + 1, NULL);	/* let it get to the sleep */
	return 0;
}


/* wake the waiters of t_q from other contexts: under a spin lock of the layer, from a softint, from a callout */
static spinlock_t t_wsl;
static void *t_sih;
static struct callout t_co;

static void
t_softint(void *cookie __unused)
{

	t_flag = 1;
	wake_up(&t_q);
}

static void
t_timer_noop(struct timer_list *t __unused)
{
}

static void
t_callout(void *cookie __unused)
{

	t_flag = 1;
	wake_up_all(&t_q);
}

/* eight waiters of the same queue, many rounds: a wake-up that is lost makes a waiter sleep the full time */
#define	T_NW	8
static volatile int t_stop, t_ready[T_NW], t_woken[T_NW];

static void
t_racer(void *cookie)
{
	const int me = (int)(uintptr_t)cookie - 1;	/* the argument is the index plus one: kthread_create passes the lwp for a null argument */
	DEFINE_WAIT(w);
	long left = 0;

	t_ready[me] = 1;
	for (;;) {
		prepare_to_wait(&t_q, &w, TASK_UNINTERRUPTIBLE);
		if (t_flag || t_stop)
			break;
		left = schedule_timeout(3 * hz);
		if (left == 0)
			break;		/* slept the whole time: the wake-up was lost */
	}
	finish_wait(&t_q, &w);
	t_woken[me] = (left == 0 && !t_flag && !t_stop) ? -1 : 1;
	kthread_exit(0);
}

int	drm2_i915_layer_c_run(void);

int
drm2_i915_layer_c_run(void)
{
	wait_queue_head_t q1;
	struct lwp *l;
	unsigned start;
	long r;
	unsigned long long ull = 5;
	int i = 5;
	long lg = 5;
	uint64_t v;
	uint64_t o;

	t_run = t_fail = 0;
	init_waitqueue_head(&t_q);
	init_waitqueue_head(&t_vq);
	init_waitqueue_head(&q1);

	/* schedule and io_schedule_timeout: a wake before the sleep means no sleep */
	set_current_state(TASK_INTERRUPTIBLE);
	wake_up_process(current);
	start = getticks();
	schedule();
	CHECK(getticks() - start < (unsigned)hz);
	set_current_state(TASK_INTERRUPTIBLE);
	wake_up_process(current);
	r = io_schedule_timeout(5 * hz);
	CHECK(r > 4 * hz && getticks() - start < (unsigned)hz);
	/* state running: no sleep either */
	set_current_state(TASK_RUNNING);
	schedule();
	r = io_schedule_timeout(3 * hz);
	CHECK(r > 2 * hz);

	/* signal_pending_state: nothing pending, in any state */
	CHECK(!signal_pending_state(TASK_UNINTERRUPTIBLE, current));
	CHECK(!signal_pending_state(TASK_INTERRUPTIBLE, current));

	/* prepare_to_wait puts the entry on the queue once and sets the state */
	{
		DEFINE_WAIT(w);
		int n = 0;
		struct wait_queue_entry *e;

		CHECK(w.private == (void *)current && list_empty(&w.entry));
		prepare_to_wait(&q1, &w, TASK_UNINTERRUPTIBLE);
		prepare_to_wait(&q1, &w, TASK_UNINTERRUPTIBLE);
		list_for_each_entry(e, &q1.head, entry)
			n++;
		CHECK(n == 1);
		/* a wake-up wakes the task (the state goes back to running) and autoremove takes the entry off */
		wake_up(&q1);
		CHECK(list_empty(&q1.head));
		start = getticks();
		r = schedule_timeout(5 * hz);
		CHECK(r > 4 * hz && getticks() - start < (unsigned)hz);
		finish_wait(&q1, &w);
		CHECK(list_empty(&w.entry));
		/* finish_wait removes an entry that is still queued */
		prepare_to_wait(&q1, &w, TASK_INTERRUPTIBLE);
		CHECK(!list_empty(&q1.head));
		finish_wait(&q1, &w);
		CHECK(list_empty(&q1.head) && list_empty(&w.entry));
		/* an entry that is not on a queue: finish_wait does nothing more */
		finish_wait(&q1, &w);
		CHECK(list_empty(&q1.head));
		/* init_wait names the current task and autoremoves */
		init_wait(&w);
		CHECK(w.private == (void *)current && w.func == autoremove_wake_function);
	}

	/* another thread in prepare_to_wait + schedule_timeout is woken by wake_up on the queue */
	CHECK(t_start(t_waiter, "t618a", &l) == 0);
	t_flag = 1;
	start = getticks();
	wake_up(&t_q);
	kthread_join(l);
	CHECK(t_done == 1 && t_left > 8 * hz && getticks() - start < (unsigned)hz);
	CHECK(list_empty(&t_q.head));

	/* ___wait_var_event: condition already true: no sleep, the given result */
	{
		long rr;

		t_var = 1;
		start = getticks();
		rr = ___wait_var_event(&t_var, t_var != 0, TASK_UNINTERRUPTIBLE, 0, 0,
		    schedule_timeout(hz));
		CHECK(rr == 0 && getticks() - start < (unsigned)(hz / 2));
		rr = ___wait_var_event(&t_var, t_var != 0, TASK_UNINTERRUPTIBLE, 0, 7,
		    schedule_timeout(hz));
		CHECK(rr == 7);
	}
	/* and woken by wake_up_all on the queue the variable maps to */
	t_var = 0;
	CHECK(t_start(t_var_waiter, "t618v", &l) == 0);
	t_var = 1;
	start = getticks();
	wake_up_all(&t_vq);
	kthread_join(l);
	CHECK(t_done == 1 && t_vret == 0 && t_left > 8 * hz && getticks() - start < (unsigned)hz);
	CHECK(list_empty(&t_vq.head));

	/* a wake-up from under a spin lock of the layer reaches a sleeper */
	CHECK(t_start(t_waiter, "t618b", &l) == 0);
	spin_lock_init(&t_wsl);
	t_flag = 1;
	start = getticks();
	spin_lock(&t_wsl);
	wake_up(&t_q);
	spin_unlock(&t_wsl);
	kthread_join(l);
	CHECK(t_done == 1 && t_left > 8 * hz && getticks() - start < (unsigned)hz);
	spin_lock_destroy(&t_wsl);

	/* from a softint */
	t_sih = softint_establish(SOFTINT_CLOCK | SOFTINT_MPSAFE, t_softint, NULL);
	CHECK(t_sih != NULL);
	CHECK(t_start(t_waiter, "t618c", &l) == 0);
	start = getticks();
	kpreempt_disable();		/* softint_schedule needs it (the layer has it through percpu_getref) */
	softint_schedule(t_sih);
	kpreempt_enable();
	kthread_join(l);
	CHECK(t_done == 1 && t_left > 8 * hz && getticks() - start < (unsigned)hz);
	softint_disestablish(t_sih);

	/* from a callout (runs in a softint) */
	callout_init(&t_co, CALLOUT_MPSAFE);
	callout_setfunc(&t_co, t_callout, NULL);
	CHECK(t_start(t_waiter, "t618d", &l) == 0);
	start = getticks();
	callout_schedule(&t_co, 1);
	kthread_join(l);
	CHECK(t_done == 1 && t_left > 8 * hz && getticks() - start < (unsigned)hz);
	callout_halt(&t_co, NULL);
	callout_destroy(&t_co);

	/* the race: 8 waiters, 100 rounds, the wake-up comes while they may be on the way to sleep */
	{
		struct lwp *lw[T_NW];
		int round, k, lost = 0, started = 0;

		start = getticks();
		for (round = 0; round < 100; round++) {
			t_flag = 0;
			t_stop = 0;
			for (k = 0; k < T_NW; k++) {
				t_ready[k] = 0;
				t_woken[k] = 0;
				if (kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN, NULL,
				    t_racer, (void *)(uintptr_t)(k + 1), &lw[k], "t618r%d", k) != 0)
					break;
			}
			started += k;
			if (round % 2)
				kpause("t618x", false, 1, NULL);
			t_flag = 1;
			wake_up_all(&t_q);
			while (k-- > 0) {
				kthread_join(lw[k]);
				if (t_woken[k] < 0)
					lost++;
			}
		}
		CHECK(started == 100 * T_NW);
		CHECK(lost == 0);
		CHECK(getticks() - start < (unsigned)(30 * hz));
		t_flag = 0;
	}

	/* try_cmpxchg64: stores when equal, else reports what it found */
	v = 10;
	o = 10;
	CHECK(try_cmpxchg64(&v, &o, 11) && v == 11 && o == 10);
	o = 10;
	CHECK(!try_cmpxchg64(&v, &o, 12) && v == 11 && o == 11);
	v = 0x100000000ull;
	o = 0x100000000ull;
	CHECK(try_cmpxchg64(&v, &o, 0xffffffff00000001ull) && v == 0xffffffff00000001ull);
	CHECK(cmpxchg64(&v, 0xffffffff00000001ull, 3) == 0xffffffff00000001ull && v == 3);

	/* wait_event_timeout evaluates its time once; overflows_type counts pgoff_t as unsigned long */
	{
		static int calls;
		wait_queue_head_t wq2;
		long rr;

		init_waitqueue_head(&wq2);
		rr = wait_event_timeout(wq2, 1, (calls++, 5));
		CHECK(calls == 1 && rr >= 1);
		rr = wait_event_interruptible_timeout(wq2, 1, (calls++, 5));
		CHECK(calls == 2 && rr >= 1);
		destroy_waitqueue_head(&wq2);
		CHECK(overflows_type(-1, pgoff_t) && !overflows_type(0x8000000000000000ull, pgoff_t));
		CHECK(!overflows_type((unsigned long)5, pgoff_t) && overflows_type((long)-5, pgoff_t));
	}

	/* castable_to_type with pgoff_t: unsigned long wide, as Linux's pgoff_t */
	CHECK(castable_to_type(5, pgoff_t) && !castable_to_type(-1, pgoff_t));
	CHECK(castable_to_type((unsigned long)i, pgoff_t));
	CHECK(castable_to_type(ull, pgoff_t));
	/* an int is too narrow; a long is the very type of the layer's pgoff_t (off_t), so it passes, which Linux's would not allow */
	CHECK(!castable_to_type(i, pgoff_t) && castable_to_type(lg, pgoff_t));
	CHECK(castable_to_type(i, int) && !castable_to_type(lg, int));


	/* static_cpu_has: the two features of the 6.18 sources are the CPUID bits, the old ones are kept, others are absent */
	CHECK(static_cpu_has(X86_FEATURE_XMM4_1) == ((cpu_info_primary.ci_feat_val[1] & CPUID2_SSE41) != 0));
	CHECK(boot_cpu_has(X86_FEATURE_HYPERVISOR) == ((cpu_info_primary.ci_feat_val[1] & CPUID2_RAZ) != 0));
	CHECK(static_cpu_has(X86_FEATURE_CLFLUSH) == cpu_has_clflush && static_cpu_has(X86_FEATURE_PAT) == cpu_has_pat);
	CHECK(!static_cpu_has(1000));

	/* static_branch_enable / disable switch a layer key */
	{
		bool key = false;

		static_branch_enable(&key);
		CHECK(key && static_branch_likely(&key));
		static_branch_disable(&key);
		CHECK(!key && !static_branch_likely(&key));
	}

	/* pci_match_id: first entry that matches, any-id entries, the class under its mask, the end of the table */
	{
		struct pci_dev pd;
		static const struct pci_device_id ids[] = {
			{ .vendor = 0x8086, .device = 0x1234, .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID },
			{ .vendor = 0x8086, .device = PCI_ANY_ID, .subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID,
			  .class = 0x030000, .class_mask = 0xff0000, .driver_data = 7 },
			{ .vendor = 0x1002, .device = 0x7d67, .subvendor = 0x17aa, .subdevice = 0x5001, .driver_data = 9 },
			{ 0 }
		};

		memset(&pd, 0, sizeof(pd));
		pd.vendor = 0x8086;
		pd.device = 0x7d67;
		pd.class = 0x030000;
		CHECK(pci_match_id(ids, &pd) == &ids[1] && pci_match_id(ids, &pd)->driver_data == 7);
		pd.device = 0x1234;
		CHECK(pci_match_id(ids, &pd) == &ids[0]);
		pd.class = 0x020000;			/* a network device: the class mask rejects entry 1 */
		pd.device = 0x7d67;
		CHECK(pci_match_id(ids, &pd) == NULL);
		pd.vendor = 0x1002;
		CHECK(pci_match_id(ids, &pd) == NULL);	/* the subsystem ids differ */
		pd.subsystem_vendor = 0x17aa;
		pd.subsystem_device = 0x5001;
		CHECK(pci_match_id(ids, &pd) == &ids[2]);
		CHECK(pci_match_id(NULL, &pd) == NULL);
	}
	CHECK(PCI_COMMAND == 4 && PCI_COMMAND_MEMORY == 2);

	/* hdmi_drm_infoframe_unpack_only reads what hdmi_drm_infoframe_pack_only wrote */
	{
		struct hdmi_drm_infoframe a, b;
		uint8_t buf[HDMI_INFOFRAME_HEADER_SIZE + HDMI_DRM_INFOFRAME_SIZE];

		CHECK(hdmi_drm_infoframe_init(&a) == 0);
		a.eotf = HDMI_EOTF_SMPTE_ST2084;
		a.metadata_type = HDMI_STATIC_METADATA_TYPE1;
		a.display_primaries[0].x = 0x1111;
		a.display_primaries[0].y = 0x2222;
		a.display_primaries[2].x = 0x3333;
		a.display_primaries[2].y = 0x4444;
		a.white_point.x = 0x5555;
		a.white_point.y = 0x6666;
		a.max_display_mastering_luminance = 1000;
		a.min_display_mastering_luminance = 50;
		a.max_cll = 800;
		a.max_fall = 400;
		CHECK(hdmi_drm_infoframe_pack_only(&a, buf, sizeof(buf)) == (int)sizeof(buf));
		memset(&b, 0xa5, sizeof(b));
		CHECK(hdmi_drm_infoframe_unpack_only(&b, buf + HDMI_INFOFRAME_HEADER_SIZE, HDMI_DRM_INFOFRAME_SIZE) == 0);
		CHECK(b.header.type == HDMI_INFOFRAME_TYPE_DRM && b.header.version == 1 &&
		    b.header.length == HDMI_DRM_INFOFRAME_SIZE);
		CHECK(b.eotf == a.eotf && b.metadata_type == a.metadata_type);
		CHECK(b.display_primaries[0].x == 0x1111 && b.display_primaries[0].y == 0x2222);
		CHECK(b.display_primaries[2].x == 0x3333 && b.display_primaries[2].y == 0x4444);
		CHECK(b.white_point.x == 0x5555 && b.white_point.y == 0x6666);
		CHECK(b.max_display_mastering_luminance == 1000 && b.min_display_mastering_luminance == 50);
		CHECK(b.max_cll == 800 && b.max_fall == 400);
		CHECK(hdmi_drm_infoframe_unpack_only(&b, buf + HDMI_INFOFRAME_HEADER_SIZE,
		    HDMI_DRM_INFOFRAME_SIZE - 1) == -EINVAL);
	}

	/* DMI_PRODUCT_SKU is a field of its own, beyond the known ones */
	CHECK(DMI_PRODUCT_SKU > DMI_STRING_MAX);


	/* xarray value entries do not collide with pointers */
	{
		int obj = 0;

		CHECK(xa_is_value(xa_mk_value(0)) && xa_is_value(xa_mk_value(12345)));
		CHECK(xa_to_value(xa_mk_value(12345)) == 12345 && xa_to_value(xa_mk_value(0)) == 0);
		CHECK(!xa_is_value(&obj) && !xa_is_value(NULL));
		CHECK(xa_to_value(xa_mk_value(~0ul >> 1)) == (~0ul >> 1));
	}

	/* xa_alloc_cyclic_irq: ids go on from the last one, wrap to the start, the limit can be full */
	{
		struct xarray xa;
		uint32_t id = 99, next = 0;
		int a, b, c, d, e;

		xa_init_flags(&xa, XA_FLAGS_ALLOC);
		a = xa_alloc_cyclic_irq(&xa, &id, &a, XA_LIMIT(1, 3), &next, GFP_KERNEL);
		CHECK(a == 0 && id == 1 && next == 2);
		b = xa_alloc_cyclic_irq(&xa, &id, &b, XA_LIMIT(1, 3), &next, GFP_KERNEL);
		CHECK(b == 0 && id == 2 && next == 3);
		c = xa_alloc_cyclic_irq(&xa, &id, &c, XA_LIMIT(1, 3), &next, GFP_KERNEL);
		CHECK(c == 0 && id == 3 && next == 4);		/* the last of the limit: no wrap yet, next is beyond the limit */
		d = xa_alloc_cyclic_irq(&xa, &id, &d, XA_LIMIT(1, 3), &next, GFP_KERNEL);
		CHECK(d < 0);					/* full */
		xa_erase(&xa, 2);
		e = xa_alloc_cyclic_irq(&xa, &id, &e, XA_LIMIT(1, 3), &next, GFP_KERNEL);
		CHECK(e == 1 && id == 2 && next == 3);		/* the second try, from the start of the limit: 1 */
		xa_erase(&xa, 1);
		xa_erase(&xa, 2);
		xa_erase(&xa, 3);
		xa_destroy(&xa);
	}

	/* bitmap regions: aligned runs of 2^order, first fit, release */
	{
		unsigned long map[1] = { 0 };
		int r1, r2, r3;

		r1 = bitmap_find_free_region(map, 16, 2);
		CHECK(r1 == 0 && map[0] == 0xf);
		r2 = bitmap_find_free_region(map, 16, 1);
		CHECK(r2 == 4 && map[0] == 0x3f);
		r3 = bitmap_find_free_region(map, 16, 3);
		CHECK(r3 == 8 && map[0] == 0xff3f);
		CHECK(bitmap_find_free_region(map, 16, 4) == -ENOMEM && map[0] == 0xff3f);
		bitmap_release_region(map, 0, 2);
		CHECK(map[0] == 0xff30);
		CHECK(bitmap_find_free_region(map, 16, 0) == 0 && map[0] == 0xff31);
		map[0] = 0xffff;
		CHECK(bitmap_find_free_region(map, 16, 0) == -ENOMEM);
		map[0] = 0x00ff;
		CHECK(bitmap_find_free_region(map, 12, 2) == 8 && map[0] == 0x0fff);
		CHECK(bitmap_find_free_region(map, 14, 2) == -ENOMEM);		/* 12..15 does not fit in 14 bits */
	}

	/* shmem: the mapping names the inode, pages read through it, memcpy to and from a page */
	{
		struct file *f = shmem_file_setup("t618", 2 * PAGE_SIZE, 0);
		struct page *pg;
		char in[16] = "i915 page copy", out[16];

		CHECK(!IS_ERR(f));
		if (!IS_ERR(f)) {
			CHECK(f->f_mapping->host == f->f_inode && f->f_mapping->host->i_size == 2 * PAGE_SIZE);
			pg = shmem_read_mapping_page(f->f_mapping, 1);
			CHECK(!IS_ERR(pg));
			if (!IS_ERR(pg)) {
				memcpy_to_page(pg, 100, in, sizeof(in));
				memset(out, 0, sizeof(out));
				memcpy_from_page(out, pg, 100, sizeof(out));
				CHECK(memcmp(in, out, sizeof(in)) == 0);
				CHECK(memcmp((char *)page_address(pg) + 100, in, sizeof(in)) == 0);
				CHECK(pg == shmem_read_mapping_page_gfp(f->f_mapping, 1, GFP_KERNEL));
			}
			fput(f);
		}
	}

	/* get_task_pid: the id of the process, as task_tgid; PGID and SID are not kept */
	{
		struct pid *p = get_task_pid(current, PIDTYPE_PID);

		CHECK(pid_nr(p) == curproc->p_pid && p == task_tgid(current));
		CHECK(get_task_pid(current, PIDTYPE_TGID) == p);
		put_pid(p);
	}

	/* the shrinker of 6.18 has the fields the driver fills in */
	{
		struct shrinker sh = { .count_objects = t_cnt, .seeks = 2, .batch = 4096, .private_data = &t_run };

		CHECK(sh.count_objects(&sh, NULL) == 0 && sh.private_data == &t_run && sh.batch == 4096);
	}

	/* vm flags and a vma that names its mapping */
	CHECK(VM_MAYWRITE == 0x20 && (VM_MAYREAD | VM_MAYEXEC | VM_MAYSHARE) == 0xd0);
	CHECK(FAULT_FLAG_RETRY_NOWAIT == 0x08 && (FAULT_FLAG_ALLOW_RETRY & FAULT_FLAG_WRITE) == 0);
	CHECK(offsetof(struct vm_area_struct, vm_mm) > offsetof(struct vm_area_struct, vm_private_data));
	CHECK(current->mm == (struct mm_struct *)curproc->p_vmspace);

	/* kfree takes a const pointer, resource_size and resource_contains too, timer_pending a const timer */
	{
		const char *cp = kstrdup("const text", GFP_KERNEL);
		const struct resource r1 = DEFINE_RES_MEM(0x1000, 0x1000), r2 = DEFINE_RES_MEM(0x1800, 0x100);
		const struct resource r3 = DEFINE_RES_MEM(0x1f80, 0x100);
		struct timer_list tm;
		const struct timer_list *ctm = &tm;

		CHECK(cp != NULL && strcmp(cp, "const text") == 0);
		kfree(cp);
		kfree((const void *)NULL);
		CHECK(resource_size(&r1) == 0x1000 && resource_size(&r2) == 0x100);
		CHECK(resource_contains(&r1, &r2) && !resource_contains(&r1, &r3) && !resource_contains(&r2, &r1));
		timer_setup(&tm, t_timer_noop, 0);	/* callout_setfunc takes no NULL function */
		CHECK(!timer_pending(ctm));
		callout_destroy(&tm.tl_callout);
	}

	/* MIPI DSI: pixel sizes, packet classes, the packet of a message */
	{
		struct mipi_dsi_packet pk;
		struct mipi_dsi_msg m;
		static const u8 two[2] = { 0x11, 0x22 };
		static const u8 five[5] = { 1, 2, 3, 4, 5 };
		int t;

		CHECK(mipi_dsi_pixel_format_to_bpp(MIPI_DSI_FMT_RGB888) == 24);
		CHECK(mipi_dsi_pixel_format_to_bpp(MIPI_DSI_FMT_RGB666) == 18 &&
		    mipi_dsi_pixel_format_to_bpp(MIPI_DSI_FMT_RGB666_PACKED) == 18);
		CHECK(mipi_dsi_pixel_format_to_bpp(MIPI_DSI_FMT_RGB565) == 16);
		CHECK(mipi_dsi_pixel_format_to_bpp((enum mipi_dsi_pixel_format)99) == -EINVAL);
		CHECK(mipi_dsi_packet_format_is_short(MIPI_DSI_DCS_SHORT_WRITE) && !mipi_dsi_packet_format_is_long(MIPI_DSI_DCS_SHORT_WRITE));
		CHECK(mipi_dsi_packet_format_is_long(MIPI_DSI_DCS_LONG_WRITE) && !mipi_dsi_packet_format_is_short(MIPI_DSI_DCS_LONG_WRITE));
		CHECK(mipi_dsi_packet_format_is_short(MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE));
		CHECK(!mipi_dsi_packet_format_is_short(0x00) && !mipi_dsi_packet_format_is_long(0x00));
		for (t = 0; t < 256; t++)
			if (mipi_dsi_packet_format_is_short(t) && mipi_dsi_packet_format_is_long(t))
				break;
		CHECK(t == 256);
		/* a short packet: channel above the type, the data in the header, no payload */
		memset(&m, 0, sizeof(m));
		m.channel = 2;
		m.type = MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM;
		m.tx_buf = two;
		m.tx_len = 2;
		memset(&pk, 0xa5, sizeof(pk));
		CHECK(mipi_dsi_create_packet(&pk, &m) == 0);
		CHECK(pk.header[0] == ((2 << 6) | 0x23) && pk.header[1] == 0x11 && pk.header[2] == 0x22 && pk.header[3] == 0);
		CHECK(pk.payload_length == 0 && pk.payload == NULL && pk.size == 4);
		m.tx_len = 1;
		m.type = MIPI_DSI_DCS_SHORT_WRITE_PARAM;
		CHECK(mipi_dsi_create_packet(&pk, &m) == 0 && pk.header[1] == 0x11 && pk.header[2] == 0);
		m.tx_len = 0;
		m.type = MIPI_DSI_TURN_ON_PERIPHERAL;
		CHECK(mipi_dsi_create_packet(&pk, &m) == 0 && pk.header[0] == ((2 << 6) | 0x32) && pk.header[1] == 0 && pk.size == 4);
		m.tx_len = 3;
		CHECK(mipi_dsi_create_packet(&pk, &m) == -EINVAL);		/* too long for a short packet */
		/* a long packet: the length in bytes 1 and 2, the payload follows */
		m.type = MIPI_DSI_DCS_LONG_WRITE;
		m.tx_buf = five;
		m.tx_len = 5;
		m.channel = 3;
		CHECK(mipi_dsi_create_packet(&pk, &m) == 0);
		CHECK(pk.header[0] == (0xc0 | 0x39) && pk.header[1] == 5 && pk.header[2] == 0);
		CHECK(pk.payload == five && pk.payload_length == 5 && pk.size == 9);
		m.tx_len = 0x1234;
		CHECK(mipi_dsi_create_packet(&pk, &m) == 0 && pk.header[1] == 0x34 && pk.header[2] == 0x12);
		m.tx_len = 0x10000;
		CHECK(mipi_dsi_create_packet(&pk, &m) == -EINVAL);
		m.tx_len = 5;
		m.channel = 4;
		CHECK(mipi_dsi_create_packet(&pk, &m) == -EINVAL);		/* only four virtual channels */
		m.channel = 0;
		m.type = 0x00;
		CHECK(mipi_dsi_create_packet(&pk, &m) == -EINVAL);		/* a type that is neither */
		CHECK(MIPI_DCS_SET_DISPLAY_BRIGHTNESS == 0x51 && MIPI_DCS_WRITE_POWER_SAVE == 0x55);
	}

	destroy_waitqueue_head(&q1);
	destroy_waitqueue_head(&t_vq);
	destroy_waitqueue_head(&t_q);
	if (t_fail != 0)
		printf("drm2-i915-layer-c: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-i915-layer-c: %d checks, 0 failed\n", t_run);
	return t_fail;
}
