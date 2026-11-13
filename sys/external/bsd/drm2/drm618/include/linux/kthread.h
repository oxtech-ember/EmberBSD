/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the Linux
 * interface, no Linux code.
 *
 * <linux/kthread.h> for the Linux 6.18 DRM core: the kthread worker API
 * (drm_vblank_work) on top of the layer's header, which the 5.6 core uses
 * and which has no workers.  The code is drm618/drm618_kthread_worker.c.
 */
#ifndef _DRM618_LINUX_KTHREAD_H_
#define _DRM618_LINUX_KTHREAD_H_

#include_next <linux/kthread.h>

#include <sys/condvar.h>
#include <sys/mutex.h>
#include <sys/types.h>

#include <linux/list.h>

struct kthread_worker;

struct kthread_work {
	struct list_head	node;
	void			(*func)(struct kthread_work *);
	struct kthread_worker	*worker;
	int			canceling;
};
typedef void (*kthread_work_func_t)(struct kthread_work *);

struct kthread_worker {
	unsigned int		flags;
	kmutex_t		lock;
	kcondvar_t		work_cv;	/* a work was queued, or stop */
	kcondvar_t		done_cv;	/* a work finished running */
	struct list_head	work_list;
	struct kthread_work	*current_work;
	bool			stop;
	struct lwp		*lwp;
};

#define	kthread_init_work(work, fn)	do {				\
	INIT_LIST_HEAD(&(work)->node);					\
	(work)->func = (fn);						\
	(work)->worker = NULL;						\
	(work)->canceling = 0;						\
} while (0)

struct kthread_worker *kthread_run_worker(unsigned int, const char *, ...)
    __printflike(2, 3);
bool	kthread_queue_work(struct kthread_worker *, struct kthread_work *);
void	kthread_flush_work(struct kthread_work *);
void	kthread_flush_worker(struct kthread_worker *);
bool	kthread_cancel_work_sync(struct kthread_work *);
void	kthread_destroy_worker(struct kthread_worker *);

#endif
