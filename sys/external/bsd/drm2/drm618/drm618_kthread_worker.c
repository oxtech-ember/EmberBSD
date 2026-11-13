/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * Kthread workers for the Linux 6.18 DRM core (drm_vblank_work): a worker is
 * a kernel thread that runs the works queued to it, one at a time, in the
 * order queued.  Interface as in Linux:
 *
 *	kthread_queue_work	true if queued; false if the work is queued
 *				already or is being cancelled.  A work that is
 *				running can be queued again.
 *	kthread_flush_work	returns when the work is neither queued nor
 *				running
 *	kthread_flush_worker	returns when everything queued before is done
 *	kthread_cancel_work_sync  removes a queued work (true) and waits for a
 *				running one; false if it was not queued
 *	kthread_destroy_worker	the works must be done; stops the thread
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kthread.h>

static void
kthread_worker_main(void *arg)
{
	struct kthread_worker *kw = arg;
	struct kthread_work *work;

	mutex_enter(&kw->lock);
	for (;;) {
		if (!list_empty(&kw->work_list)) {
			work = list_first_entry(&kw->work_list,
			    struct kthread_work, node);
			list_del_init(&work->node);
			kw->current_work = work;
			mutex_exit(&kw->lock);
			(*work->func)(work);
			mutex_enter(&kw->lock);
			kw->current_work = NULL;
			cv_broadcast(&kw->done_cv);
			continue;
		}
		if (kw->stop)
			break;
		cv_wait(&kw->work_cv, &kw->lock);
	}
	mutex_exit(&kw->lock);
	kthread_exit(0);
}

struct kthread_worker *
kthread_run_worker(unsigned int flags, const char *fmt, ...)
{
	struct kthread_worker *kw;
	char name[MAXCOMLEN + 1];
	va_list ap;
	int error;

	kw = kmem_zalloc(sizeof(*kw), KM_SLEEP);
	kw->flags = flags;
	mutex_init(&kw->lock, MUTEX_DEFAULT, IPL_NONE);
	cv_init(&kw->work_cv, "kwwork");
	cv_init(&kw->done_cv, "kwdone");
	INIT_LIST_HEAD(&kw->work_list);

	va_start(ap, fmt);
	vsnprintf(name, sizeof(name), fmt, ap);
	va_end(ap);

	error = kthread_create(PRI_NONE, KTHREAD_MPSAFE | KTHREAD_MUSTJOIN,
	    NULL, kthread_worker_main, kw, &kw->lwp, "%s", name);
	if (error) {
		cv_destroy(&kw->done_cv);
		cv_destroy(&kw->work_cv);
		mutex_destroy(&kw->lock);
		kmem_free(kw, sizeof(*kw));
		return ERR_PTR(-error);
	}
	/* The task of the worker is its lwp, for sched_set_fifo() only. */
	kw->task = (struct task_struct *)kw->lwp;
	return kw;
}

/*
 * The thread of a worker runs at the lowest kernel real time priority
 * (Linux: SCHED_FIFO).  NULL: nothing to do.
 */
void
sched_set_fifo(struct task_struct *task)
{
	struct lwp *l = (struct lwp *)task;

	if (l == NULL)
		return;
	lwp_lock(l);
	lwp_changepri(l, PRI_KERNEL_RT);
	lwp_unlock(l);
}

bool
kthread_queue_work(struct kthread_worker *kw, struct kthread_work *work)
{
	bool queued = false;

	mutex_enter(&kw->lock);
	if (list_empty(&work->node) && work->canceling == 0) {
		work->worker = kw;
		list_add_tail(&work->node, &kw->work_list);
		cv_signal(&kw->work_cv);
		queued = true;
	}
	mutex_exit(&kw->lock);
	return queued;
}

void
kthread_flush_work(struct kthread_work *work)
{
	struct kthread_worker *kw = work->worker;

	if (kw == NULL)		/* never queued */
		return;
	mutex_enter(&kw->lock);
	while (!list_empty(&work->node) || kw->current_work == work)
		cv_wait(&kw->done_cv, &kw->lock);
	mutex_exit(&kw->lock);
}

struct flush_work {
	struct kthread_work	work;
	bool			done;
	kmutex_t		*lock;
	kcondvar_t		*cv;
};

static void
flush_work_func(struct kthread_work *work)
{
	struct flush_work *fw = (struct flush_work *)work;

	/* the worker's lock is not held here: take it for the flag */
	mutex_enter(fw->lock);
	fw->done = true;
	cv_broadcast(fw->cv);
	mutex_exit(fw->lock);
}

void
kthread_flush_worker(struct kthread_worker *kw)
{
	struct flush_work fw;

	kthread_init_work(&fw.work, flush_work_func);
	fw.done = false;
	fw.lock = &kw->lock;
	fw.cv = &kw->done_cv;
	(void)kthread_queue_work(kw, &fw.work);
	mutex_enter(&kw->lock);
	while (!fw.done)
		cv_wait(&kw->done_cv, &kw->lock);
	mutex_exit(&kw->lock);
	/* the worker is past flush_work_func, but not yet out of the loop */
	kthread_flush_work(&fw.work);
}

bool
kthread_cancel_work_sync(struct kthread_work *work)
{
	struct kthread_worker *kw = work->worker;
	bool pending = false;

	if (kw == NULL)		/* never queued */
		return false;
	mutex_enter(&kw->lock);
	if (!list_empty(&work->node)) {
		list_del_init(&work->node);
		pending = true;
	}
	work->canceling++;
	while (kw->current_work == work)
		cv_wait(&kw->done_cv, &kw->lock);
	work->canceling--;
	cv_broadcast(&kw->done_cv);
	mutex_exit(&kw->lock);
	return pending;
}

void
kthread_destroy_worker(struct kthread_worker *kw)
{

	mutex_enter(&kw->lock);
	KASSERT(list_empty(&kw->work_list));
	kw->stop = true;
	cv_signal(&kw->work_cv);
	mutex_exit(&kw->lock);
	kthread_join(kw->lwp);
	cv_destroy(&kw->done_cv);
	cv_destroy(&kw->work_cv);
	mutex_destroy(&kw->lock);
	kmem_free(kw, sizeof(*kw));
}
