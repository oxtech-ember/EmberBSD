/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Shadow of the shared
 * <linux/sched/signal.h> for the 6.18 core: the task is a struct task_struct
 * here (linux/sched.h), only the current one.
 */
#ifndef _DRM618_LINUX_SCHED_SIGNAL_H_
#define _DRM618_LINUX_SCHED_SIGNAL_H_

#include <sys/lwp.h>
#include <sys/signalvar.h>

#include <linux/sched.h>

static inline bool
signal_pending(struct task_struct *task)
{

	KASSERT(task == current);
	return __predict_false(curlwp->l_flag & LW_PENDSIG) &&
	    sigispending(curlwp, 0);
}

#endif
