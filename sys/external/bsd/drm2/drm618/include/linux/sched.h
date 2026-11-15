/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Shadow of the shared <linux/sched.h> for the 6.18 core: what it reads of
 * the current task.  The layer's `current' is NetBSD's curproc, a struct proc;
 * the 6.18 sources read current->comm and ask for task ids.
 *
 * struct task_struct is a view of struct proc: its member comm is at the
 * place of p_comm, so (struct task_struct *)proc->comm is the name of the
 * process.  Nothing else of it is read.  A task here is a process: the
 * thread group id is the process id.
 *
 * struct pid is not an object here but the process id itself, carried in
 * the pointer (the ids are positive).  get_pid and put_pid do nothing and a
 * stored pid keeps naming the id if the process is gone and the id reused,
 * where Linux keeps the id from being reused while it is referenced.  The
 * 6.18 core uses a pid to name the owner of an open file in messages and
 * to tell whether the caller is the one that opened it.
 */

#ifndef _DRM618_LINUX_SCHED_H_
#define _DRM618_LINUX_SCHED_H_

/* the shared one takes a struct proc; this header has one for a task below */
#define	signal_pending_state	drm618_shared_signal_pending_state
#include_next <linux/sched.h>
#undef	signal_pending_state

#include <sys/stddef.h>
#include <sys/proc.h>
#include <sys/lwp.h>
#include <sys/signalvar.h>

struct mm_struct;

struct task_struct {
	union {
		struct {
			char	__proc_before_comm[offsetof(struct proc, p_comm)];
			char	comm[MAXCOMLEN + 1];
		};
		struct {
			char	__proc_before_pid[offsetof(struct proc, p_pid)];
			union {
				pid_t	pid;
				pid_t	tgid;	/* a task is a process here */
			};
		};
		/*
		 * Identity of the thread group (the process).  It is only
		 * compared (drm_sched_entity: who queued last), never
		 * dereferenced, so the address of the process's vmspace
		 * stands for it: one per process, stable while the process
		 * runs (an exec gives the process a new one, which only
		 * makes a flush see "another user", as after a fork).
		 */
		struct {
			char	__proc_before_gl[offsetof(struct proc, p_vmspace)];
			struct task_struct *group_leader;
		};
		/* The address space of the process: its vmspace (an identity here, the user map is not reached through it). */
		struct {
			char	__proc_before_mm[offsetof(struct proc, p_vmspace)];
			struct mm_struct *mm;
		};
		/* PF_EXITING: the process is exiting (PS_WEXIT of p_sflag). */
		struct {
			char	__proc_before_flags[offsetof(struct proc, p_sflag)];
			unsigned int flags;
		};
		/* exit_code: the signal that ends the process (p_xsig), 0 while it runs. */
		struct {
			char	__proc_before_xsig[offsetof(struct proc, p_xsig)];
			int	exit_code;
		};
	};
};

#define	PF_EXITING	PS_WEXIT

#undef	current
#define	current	((struct task_struct *)curproc)

struct pid;

enum pid_type {
	PIDTYPE_PID,
	PIDTYPE_TGID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
};

/* The layer's task_pid_nr takes the struct proc that `current' was; here a task_struct. */
#define	task_pid_nr(task)	task_pid_vnr(task)

static inline pid_t
task_pid_vnr(struct task_struct *task)
{

	return ((struct proc *)task)->p_pid;
}

static inline pid_t
task_tgid_nr(struct task_struct *task)
{

	return ((struct proc *)task)->p_pid;
}

static inline struct pid *
task_tgid(struct task_struct *task)
{

	return (struct pid *)(uintptr_t)task_tgid_nr(task);
}

static inline struct pid *
get_pid(struct pid *pid)
{

	return pid;
}

static inline void
put_pid(struct pid *pid)
{
}

static inline pid_t
pid_nr(struct pid *pid)
{

	return (pid_t)(uintptr_t)pid;
}
/*
 * The pid of a task: the process id, for the id of the task and of its thread
 * group (the same here).  The ids of the process group and the session are not
 * kept.
 */
static inline struct pid *
get_task_pid(struct task_struct *task, enum pid_type type)
{

	KASSERT(type == PIDTYPE_PID || type == PIDTYPE_TGID);
	return get_pid(task_tgid(task));
}


/*
 * The task of a pid: only the caller's own is found (a lookup of another
 * process needs proc_lock and a reference that this interface cannot hand
 * out); NULL for any other.
 */
static inline struct task_struct *
pid_task(struct pid *pid, enum pid_type type)
{

	if (pid != NULL && pid_nr(pid) == curproc->p_pid)
		return current;
	return NULL;
}

/* The name of the process into buf (an array); buf. */
#define	get_task_comm(buf, tsk)	({					\
	strlcpy((buf), (tsk)->comm, sizeof(buf));			\
	(buf);								\
})

/* The id user space sees for a user id that does not map (65534). */
#define	overflowuid	65534

/*
 * The sleeping protocol (drm618_sleep.c): the state is set, the condition
 * tested, then schedule_timeout sleeps unless wake_up_process came in
 * between.  schedule_timeout returns the time left (MAX_SCHEDULE_TIMEOUT for
 * an unbounded sleep).
 */
#define	TASK_RUNNING	0

void	drm618_set_current_state(int);
long	schedule_timeout(long);
int	wake_up_process(struct task_struct *);

static inline void
schedule(void)
{

	(void)schedule_timeout(MAX_SCHEDULE_TIMEOUT);
}

static inline long
io_schedule_timeout(long timeout)
{

	return schedule_timeout(timeout);
}

static inline void
io_schedule(void)
{

	schedule();
}

/* A signal ends a sleep in the interruptible states only. */
static inline bool
signal_pending_state(int state, struct task_struct *task)
{

	KASSERT((struct proc *)task == curproc);
	if (state & TASK_UNINTERRUPTIBLE)
		return false;
	return sigispending(curlwp, 0);
}

#define	set_current_state(state)	drm618_set_current_state(state)
#define	__set_current_state(state)	drm618_set_current_state(state)

/* No lock validator here. */
#define	lockdep_assert_none_held_once()	((void)0)

#endif	/* _DRM618_LINUX_SCHED_H_ */
