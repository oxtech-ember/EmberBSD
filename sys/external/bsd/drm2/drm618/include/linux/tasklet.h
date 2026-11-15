/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/i915_tasklet.h, gt/intel_engine_cs.c,
 * gt/intel_execlists_submission.c, gt/uc/intel_guc_submission.c, i915_scheduler_types.h (how the
 * structure and its calls are used); the layer's common/include/linux/tasklet.h for the names (BSD); the GPL original was not read.
 *
 * The tasklet of the Linux 6.18 sources: the state bits and the disable
 * count are members the driver reads itself.  Not the layer's NetBSD
 * tasklet (that structure has other members): a 6.18 file that includes
 * <linux/interrupt.h> gets this one.  The inline functions need nothing but
 * the bits and the count.  Queueing (tasklet_schedule, tasklet_hi_schedule),
 * waiting for a running tasklet (tasklet_kill, tasklet_disable) and the setup
 * are declared and need the softint implementation: not yet defined, a module that
 * calls them does not link (see drm618/README: declared only).
 */
#ifndef _DRM618_LINUX_TASKLET_H_
#define _DRM618_LINUX_TASKLET_H_

#include <sys/types.h>

#include <linux/atomic.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/container_of.h>

enum {
	TASKLET_STATE_SCHED,	/* queued */
	TASKLET_STATE_RUN,	/* running */
};

struct tasklet_struct {
	struct tasklet_struct	*next;
	unsigned long		state;
	atomic_t		count;
	bool			use_callback;
	union {
		void		(*func)(unsigned long);
		void		(*callback)(struct tasklet_struct *);
	};
	unsigned long		data;
};

#define	DECLARE_TASKLET(name, _callback)				\
	struct tasklet_struct name = {					\
		.count = ATOMIC_INIT(0),				\
		.callback = (_callback),				\
		.use_callback = true,					\
	}

#define	from_tasklet(var, callback_tasklet, tasklet_fieldname)		\
	container_of(callback_tasklet, __typeof__(*(var)), tasklet_fieldname)

#define	tasklet_setup		drm618_tasklet_setup
#define	tasklet_init		drm618_tasklet_init
#define	tasklet_schedule	drm618_tasklet_schedule
#define	tasklet_hi_schedule	drm618_tasklet_hi_schedule
#define	tasklet_kill		drm618_tasklet_kill
#define	tasklet_disable		drm618_tasklet_disable

void	drm618_tasklet_setup(struct tasklet_struct *,
	    void (*)(struct tasklet_struct *));
void	drm618_tasklet_init(struct tasklet_struct *, void (*)(unsigned long),
	    unsigned long);
void	drm618_tasklet_schedule(struct tasklet_struct *);
void	drm618_tasklet_hi_schedule(struct tasklet_struct *);
void	drm618_tasklet_kill(struct tasklet_struct *);
void	drm618_tasklet_disable(struct tasklet_struct *);

/* the tasklet does not run while the count is not zero */
static inline void
tasklet_disable_nosync(struct tasklet_struct *t)
{

	atomic_inc(&t->count);
}

static inline void
tasklet_enable(struct tasklet_struct *t)
{

	atomic_dec(&t->count);
}

static inline bool
tasklet_trylock(struct tasklet_struct *t)
{

	return !test_and_set_bit(TASKLET_STATE_RUN, &t->state);
}

static inline void
tasklet_unlock(struct tasklet_struct *t)
{

	clear_bit_unlock(TASKLET_STATE_RUN, &t->state);
}

static inline void
tasklet_unlock_spin_wait(struct tasklet_struct *t)
{

	while (test_bit(TASKLET_STATE_RUN, &t->state))
		__insn_barrier();
}

static inline void
tasklet_unlock_wait(struct tasklet_struct *t)
{

	tasklet_unlock_spin_wait(t);
}

#endif
