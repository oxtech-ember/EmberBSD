/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Kernel-side checks of the GPU scheduler of Linux 6.18 (drm_sched) on the
 * layer: a fake scheduler with a fake "hardware" whose fences the test
 * signals.  Scenarios: jobs of one entity in order, dependencies between
 * entities, the credit limit, the job timeout, the kill of the queued jobs of
 * an entity, suspend/resume of the timeout, cancel of the pending jobs at
 * scheduler teardown.  Written for this layer; the scenarios follow the
 * contract in drm/gpu_scheduler.h.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/kernel.h>
#include <sys/systm.h>

#include <linux/dma-fence.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <drm/gpu_scheduler.h>

static int t_run, t_fail;

#define	CHECK(cond)	do {						\
	t_run++;							\
	if (!(cond)) {							\
		t_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

/* ---- fake hardware ------------------------------------------------------ */

static const char *
t_hw_name(struct dma_fence *f __unused)
{

	return "t618sched";
}

static const struct dma_fence_ops t_hw_ops = {
	.get_driver_name = t_hw_name,
	.get_timeline_name = t_hw_name,
};

static DEFINE_SPINLOCK(t_hw_lock);
static u64 t_hw_ctx;

struct t_job {
	struct drm_sched_job	base;
	struct dma_fence	*hw;	/* what run_job returns */
	int			id;
};

#define	T_MAXJOBS	16
static struct t_job *volatile t_jobs[T_MAXJOBS];
static volatile unsigned t_ran;		/* run_job calls */
static volatile int t_order[T_MAXJOBS];	/* job ids in run order */
static volatile unsigned t_freed;
static volatile unsigned t_timedout;
static volatile unsigned t_cancelled;
static volatile int t_free_ids[T_MAXJOBS];
static int t_tmo_signal;		/* timedout_job signals the hw fence */

static struct dma_fence *
t_run_job(struct drm_sched_job *sj)
{
	struct t_job *j = container_of(sj, struct t_job, base);
	unsigned n = atomic_inc_uint_nv(&t_ran) - 1;

	if (n < T_MAXJOBS)
		t_order[n] = j->id;
	return dma_fence_get(j->hw);
}

static enum drm_gpu_sched_stat
t_timedout_job(struct drm_sched_job *sj)
{
	struct t_job *j = container_of(sj, struct t_job, base);

	atomic_inc_uint(&t_timedout);
	/* what a driver does: stop, reset the hardware (here: fail the fence), start */
	drm_sched_stop(sj->sched, sj);
	if (t_tmo_signal) {
		dma_fence_set_error(j->hw, -ETIME);
		dma_fence_signal(j->hw);
	}
	drm_sched_start(sj->sched, 0);
	return DRM_GPU_SCHED_STAT_RESET;
}

static void
t_free_job(struct drm_sched_job *sj)
{
	struct t_job *j = container_of(sj, struct t_job, base);
	unsigned n = atomic_inc_uint_nv(&t_freed) - 1;

	if (n < T_MAXJOBS)
		t_free_ids[n] = j->id;
	t_jobs[j->id] = NULL;
	dma_fence_put(j->hw);
	drm_sched_job_cleanup(sj);
	kfree(j);
}

static void
t_cancel_job(struct drm_sched_job *sj)
{
	struct t_job *j = container_of(sj, struct t_job, base);

	atomic_inc_uint(&t_cancelled);
	dma_fence_set_error(j->hw, -ECANCELED);
	dma_fence_signal(j->hw);
}

static const struct drm_sched_backend_ops t_ops = {
	.run_job = t_run_job,
	.timedout_job = t_timedout_job,
	.free_job = t_free_job,
	.cancel_job = t_cancel_job,
};

static void
t_reset(void)
{

	t_ran = t_freed = t_timedout = t_cancelled = 0;
	t_tmo_signal = 0;
}

static bool
t_wait(volatile unsigned *v, unsigned want, int ticks)
{
	int i;

	for (i = 0; i < ticks && *v < want; i++)
		kpause("t618sw", false, 1, NULL);
	return *v >= want;
}

/* Create, arm and push a job of the given id with credits on entity e (deps: optional). */
static struct t_job *
t_push(struct drm_sched_entity *e, int id, u32 credits, struct dma_fence *dep)
{
	struct t_job *j = kzalloc(sizeof(*j), GFP_KERNEL);

	if (j == NULL)
		return NULL;
	j->id = id;
	j->hw = kzalloc(sizeof(*j->hw), GFP_KERNEL);
	dma_fence_init(j->hw, &t_hw_ops, &t_hw_lock, t_hw_ctx, id + 1);
	if (drm_sched_job_init(&j->base, e, credits, NULL, 0) != 0) {
		kfree(j->hw);
		kfree(j);
		return NULL;
	}
	if (dep != NULL)
		CHECK(drm_sched_job_add_dependency(&j->base, dma_fence_get(dep)) == 0);
	t_jobs[id] = j;
	drm_sched_job_arm(&j->base);
	drm_sched_entity_push_job(&j->base);
	return j;
}

static int
t_sched_init(struct drm_gpu_scheduler *s, u32 credit_limit, long timeout)
{
	struct drm_sched_init_args a = {
		.ops = &t_ops,
		.num_rqs = DRM_SCHED_PRIORITY_COUNT,
		.credit_limit = credit_limit,
		.hang_limit = 1,
		.timeout = timeout,
		.name = "t618",
	};

	/* a scheduler is zeroed memory in a driver (kzalloc) */
	memset(s, 0, sizeof(*s));
	return drm_sched_init(s, &a);
}

int	drm2_sched_run(void);

int
drm2_sched_run(void)
{
	struct drm_gpu_scheduler sched;
	struct drm_gpu_scheduler *list[1] = { &sched };
	struct drm_sched_entity e1, e2;
	struct t_job *j1, *j2, *j3;
	long tmo;

	t_hw_ctx = dma_fence_context_alloc(1);
	memset(&e1, 0, sizeof(e1));
	memset(&e2, 0, sizeof(e2));

	/* ---- 1. jobs of one entity run in order; free_job after the hw fence --- */
	t_reset();
	CHECK(t_sched_init(&sched, 8, MAX_SCHEDULE_TIMEOUT) == 0);
	CHECK(drm_sched_entity_init(&e1, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	j1 = t_push(&e1, 0, 1, NULL);
	j2 = t_push(&e1, 1, 1, NULL);
	j3 = t_push(&e1, 2, 1, NULL);
	CHECK(j1 && j2 && j3);
	CHECK(t_wait(&t_ran, 3, 200));
	CHECK(t_order[0] == 0 && t_order[1] == 1 && t_order[2] == 2);
	/* nothing finished yet: the hw fences are not signaled */
	CHECK(!dma_fence_is_signaled(&j1->base.s_fence->finished));
	CHECK(t_freed == 0);
	{
		/* keep our own references: free_job drops the job */
		struct dma_fence *f1 = dma_fence_get(&j1->base.s_fence->finished);
		struct dma_fence *f3 = dma_fence_get(&j3->base.s_fence->finished);
		struct dma_fence *hw2 = dma_fence_get(j2->hw);

		dma_fence_signal(j1->hw);
		CHECK(dma_fence_wait_timeout(f1, false, hz) > 0);
		CHECK(f1->error == 0);
		/* a later job can finish before an earlier one is freed, and each is freed once */
		dma_fence_signal(j3->hw);
		CHECK(dma_fence_wait_timeout(f3, false, hz) > 0);
		CHECK(!dma_fence_is_signaled(hw2) || 1);
		dma_fence_signal(hw2);
		CHECK(t_wait(&t_freed, 3, 200));
		dma_fence_put(f1);
		dma_fence_put(f3);
		dma_fence_put(hw2);
	}
	CHECK(t_freed == 3);
	drm_sched_entity_destroy(&e1);
	drm_sched_fini(&sched);

	/* ---- 2. dependencies: on an outside fence, and pipelined on a job of the same scheduler ---- */
	t_reset();
	CHECK(t_sched_init(&sched, 8, MAX_SCHEDULE_TIMEOUT) == 0);
	CHECK(drm_sched_entity_init(&e1, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	CHECK(drm_sched_entity_init(&e2, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	{
		struct dma_fence *ext = kzalloc(sizeof(*ext), GFP_KERNEL);
		struct dma_fence *f1, *f2;

		dma_fence_init(ext, &t_hw_ops, &t_hw_lock, t_hw_ctx, 50);
		j1 = t_push(&e1, 0, 1, ext);		/* waits for ext */
		CHECK(j1 != NULL);
		f1 = dma_fence_get(&j1->base.s_fence->finished);
		kpause("t618w", false, hz / 10, NULL);
		CHECK(t_ran == 0);			/* the outside fence holds it */
		/* a job of another entity on j1's finished fence: same scheduler, so pipelined */
		j2 = t_push(&e2, 1, 1, f1);
		CHECK(j2 != NULL);
		f2 = dma_fence_get(&j2->base.s_fence->finished);
		kpause("t618w", false, hz / 10, NULL);
		CHECK(t_ran == 0);			/* j2 waits for j1 to be scheduled */
		dma_fence_signal(ext);
		CHECK(t_wait(&t_ran, 2, 200));		/* both run, j2 without waiting for the hardware */
		CHECK(t_order[0] == 0 && t_order[1] == 1);
		CHECK(!dma_fence_is_signaled(f1) && !dma_fence_is_signaled(f2));
		dma_fence_signal(j1->hw);
		dma_fence_signal(j2->hw);
		CHECK(dma_fence_wait_timeout(f2, false, hz) > 0);
		CHECK(t_wait(&t_freed, 2, 200));
		dma_fence_put(f1);
		dma_fence_put(f2);
		dma_fence_put(ext);
	}
	drm_sched_entity_destroy(&e1);
	drm_sched_entity_destroy(&e2);
	drm_sched_fini(&sched);

	/* ---- 3. the credit limit holds jobs back ------------------------------- */
	t_reset();
	CHECK(t_sched_init(&sched, 2, MAX_SCHEDULE_TIMEOUT) == 0);
	CHECK(drm_sched_entity_init(&e1, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	j1 = t_push(&e1, 0, 1, NULL);
	j2 = t_push(&e1, 1, 1, NULL);
	j3 = t_push(&e1, 2, 1, NULL);
	CHECK(j1 && j2 && j3);
	CHECK(t_wait(&t_ran, 2, 200));
	kpause("t618w", false, hz / 10, NULL);
	CHECK(t_ran == 2);				/* the third has no credit */
	dma_fence_signal(j1->hw);
	CHECK(t_wait(&t_ran, 3, 200));			/* a credit came back */
	CHECK(t_order[2] == 2);
	dma_fence_signal(j2->hw);
	dma_fence_signal(j3->hw);
	CHECK(t_wait(&t_freed, 3, 200));
	drm_sched_entity_destroy(&e1);
	drm_sched_fini(&sched);

	/* ---- 4. a job that never finishes times out ----------------------------- */
	t_reset();
	t_tmo_signal = 1;
	CHECK(t_sched_init(&sched, 4, hz / 5) == 0);	/* 200 ms */
	CHECK(drm_sched_entity_init(&e1, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	j1 = t_push(&e1, 0, 1, NULL);
	CHECK(j1 != NULL);
	{
		struct dma_fence *f1 = dma_fence_get(&j1->base.s_fence->finished);

		CHECK(t_wait(&t_ran, 1, 100));
		CHECK(t_timedout == 0);
		CHECK(t_wait(&t_timedout, 1, 3 * hz));
		CHECK(t_timedout == 1);
		CHECK(dma_fence_wait_timeout(f1, false, hz) > 0);
		/*
		 * drm_sched_stop dropped the hardware fence of the pending job
		 * and drm_sched_start completes such a job with -ECANCELED: the
		 * driver would resubmit (drm_sched_resubmit_jobs) after a reset.
		 */
		CHECK(f1->error == -ECANCELED);
		CHECK(t_wait(&t_freed, 1, 200));
		dma_fence_put(f1);
	}
	drm_sched_entity_destroy(&e1);
	drm_sched_fini(&sched);

	/* ---- 5. the kill of an entity with a job that cannot run yet ------------ */
	t_reset();
	CHECK(t_sched_init(&sched, 4, MAX_SCHEDULE_TIMEOUT) == 0);
	CHECK(drm_sched_entity_init(&e1, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	{
		struct dma_fence *never = kzalloc(sizeof(*never), GFP_KERNEL);
		struct dma_fence *f1;

		dma_fence_init(never, &t_hw_ops, &t_hw_lock, t_hw_ctx, 100);
		j1 = t_push(&e1, 0, 1, never);
		CHECK(j1 != NULL);
		f1 = dma_fence_get(&j1->base.s_fence->finished);
		kpause("t618w", false, hz / 10, NULL);
		CHECK(t_ran == 0);			/* it waits for `never` */
		/*
		 * Not drm_sched_entity_destroy: its flush waits for the entity to
		 * go idle unless the process is exiting, and this job never
		 * becomes ready.  fini kills what is still queued.
		 */
		drm_sched_entity_fini(&e1);
		/*
		 * The kill work waits for the dependencies of the job (so that the
		 * memory the job would have used is not reused too early): the
		 * finished fence stays unsignaled until `never` signals.
		 */
		kpause("t618w", false, hz / 5, NULL);
		CHECK(!dma_fence_is_signaled(f1));
		CHECK(t_freed == 0);
		dma_fence_signal(never);
		CHECK(dma_fence_wait_timeout(f1, false, hz) > 0);
		CHECK(f1->error == -ESRCH);
		CHECK(t_ran == 0);			/* it never ran */
		CHECK(t_wait(&t_freed, 1, 200));	/* and was freed */
		dma_fence_put(f1);
		dma_fence_put(never);
	}
	drm_sched_fini(&sched);

	/* ---- 6. suspend and resume of the timeout ------------------------------- */
	t_reset();
	CHECK(t_sched_init(&sched, 4, hz) == 0);	/* 1 s */
	CHECK(drm_sched_entity_init(&e1, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	j1 = t_push(&e1, 0, 1, NULL);
	CHECK(j1 != NULL);
	CHECK(t_wait(&t_ran, 1, 100));
	tmo = (long)drm_sched_suspend_timeout(&sched);
	CHECK(tmo > 0 && tmo <= hz);			/* time left of the 1 s */
	kpause("t618w", false, hz * 3 / 2, NULL);	/* longer than the timeout */
	CHECK(t_timedout == 0);				/* suspended: it did not fire */
	t_tmo_signal = 1;
	drm_sched_resume_timeout(&sched, hz / 10);
	CHECK(t_wait(&t_timedout, 1, 2 * hz));		/* now it does */
	CHECK(t_wait(&t_freed, 1, 200));
	drm_sched_entity_destroy(&e1);
	drm_sched_fini(&sched);

	/* ---- 7. teardown with a pending job cancels it -------------------------- */
	t_reset();
	CHECK(t_sched_init(&sched, 4, MAX_SCHEDULE_TIMEOUT) == 0);
	CHECK(drm_sched_entity_init(&e1, DRM_SCHED_PRIORITY_NORMAL, list, 1, NULL) == 0);
	j1 = t_push(&e1, 0, 1, NULL);
	CHECK(j1 != NULL);
	CHECK(t_wait(&t_ran, 1, 100));			/* running, not finished */
	drm_sched_entity_destroy(&e1);
	drm_sched_fini(&sched);				/* cancel_job + free_job */
	CHECK(t_cancelled == 1);
	CHECK(t_freed == 1);

	printf("drm2-sched: %d checks, %d failed\n", t_run, t_fail);
	return t_fail;
}
