/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the sleeping model of tasks (set_current_state,
 * schedule_timeout, wake_up_process; drm618_sleep.c) and of sync files
 * (drm618_sync_file.c).
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/filedesc.h>
#include <sys/kthread.h>
#include <sys/proc.h>
#include <sys/systm.h>

#include <linux/errno.h>
#include <linux/dma-fence.h>
#include <linux/file.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/sync_file.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static volatile int t_sleeping, t_done;
static long t_remaining;
static long t_arg;

/* A kernel thread (a task of proc0) that sleeps once. */
static void
t_sleeper(void *cookie __unused)
{

	set_current_state(TASK_INTERRUPTIBLE);
	t_sleeping = 1;
	t_remaining = schedule_timeout(t_arg);
	__set_current_state(TASK_RUNNING);
	t_done = 1;
	kthread_exit(0);
}

static long
run_sleeper(long timeout)
{
	struct lwp *l;
	long woken_after;
	unsigned start;

	t_sleeping = t_done = 0;
	t_arg = timeout;
	if (kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN, NULL,
	    t_sleeper, NULL, &l, "t618sl") != 0)
		return -1;
	while (!t_sleeping)
		kpause("t618w", false, 1, NULL);
	kpause("t618w", false, hz / 20 + 1, NULL);	/* let it get into schedule_timeout */
	start = getticks();
	wake_up_process((struct task_struct *)&proc0);
	kthread_join(l);
	woken_after = getticks() - start;
	(void)woken_after;
	return t_remaining;
}

static const char *t_name(struct dma_fence *f) { return "t618"; }
static const struct dma_fence_ops t_fence_ops = {
	.get_driver_name = t_name,
	.get_timeline_name = t_name,
};

static const struct file_operations t_other_fops;

int	drm2_drm618_task_run(void);

int
drm2_drm618_task_run(void)
{
	unsigned start;
	long r;
	struct sync_file *sf;
	struct file *f;
	struct dma_fence fence;
	spinlock_t lock;
	int fd;

	t_run = t_fail = 0;

	/* a wake before the sleep: the sleep does not happen */
	set_current_state(TASK_INTERRUPTIBLE);
	wake_up_process(current);
	start = getticks();
	r = schedule_timeout(5 * hz);
	CHECK(r > 4 * hz);
	CHECK(getticks() - start < (unsigned)hz);

	/* the state set back to running: no sleep either */
	set_current_state(TASK_INTERRUPTIBLE);
	__set_current_state(TASK_RUNNING);
	r = schedule_timeout(5 * hz);
	CHECK(r > 4 * hz);

	/* a sleep that times out: nothing left, at least the time passed */
	set_current_state(TASK_UNINTERRUPTIBLE);
	start = getticks();
	r = schedule_timeout(hz / 10 + 1);
	CHECK(r == 0);
	CHECK(getticks() - start >= (unsigned)(hz / 10));

	/* woken from another thread: time is left */
	r = run_sleeper(5 * hz);
	CHECK(r > 0 && r < 5 * hz);
	CHECK(t_done);

	/* an unbounded sleep ends with the wake and says so */
	r = run_sleeper(MAX_SCHEDULE_TIMEOUT);
	CHECK(r == MAX_SCHEDULE_TIMEOUT);

	/* wake_up_process of a task that does not sleep: harmless */
	wake_up_process((struct task_struct *)&proc0);

	/* sync file: not readable until the fence is signalled */
	spin_lock_init(&lock);
	dma_fence_init(&fence, &t_fence_ops, &lock, 1, 1);
	sf = sync_file_create(&fence);
	CHECK(sf != NULL);
	f = sf->file;
	CHECK(f->f_op->poll != NULL);
	CHECK(f->f_op->poll(f, NULL) == 0);
	dma_fence_signal(&fence);
	CHECK((f->f_op->poll(f, NULL) & EPOLLIN) != 0);

	/* through a descriptor */
	fd = get_unused_fd_flags(O_CLOEXEC);
	CHECK(fd >= 0);
	fd_install(fd, f);
	{
		struct dma_fence *got = sync_file_get_fence(fd);

		CHECK(got == &fence);
		dma_fence_put(got);
	}
	CHECK(sync_file_get_fence(fd + 1000) == NULL);
	CHECK(fd_getfile(fd) != NULL);
	CHECK(fd_close(fd) == 0);	/* release drops the sync file's fence reference */
	CHECK(kref_read(&fence.refcount) == 1);

	/* a file that is not a sync file */
	{
		struct file *g = anon_inode_getfile("x", &t_other_fops, NULL, O_RDWR);
		int fd2 = get_unused_fd_flags(0);

		fd_install(fd2, g);
		CHECK(sync_file_get_fence(fd2) == NULL);
		CHECK(fd_getfile(fd2) != NULL);
		CHECK(fd_close(fd2) == 0);
	}

	if (t_fail != 0)
		printf("drm2-drm618-task: %d of %d checks FAILED\n", t_fail, t_run);
	else
		printf("drm2-drm618-task: %d checks, 0 failed\n", t_run);
	return t_fail;
}
