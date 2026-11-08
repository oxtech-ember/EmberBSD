/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#define _POSIX_C_SOURCE 200809L
#include "drm-scheduler-model.h"
#include "sched-fence-body.h"
#include "sched-main-body.h"
#include "sched-entity-body.h"

struct test_job { struct drm_sched_job base; unsigned order; int error; };
static struct completion drained, release_free, free_entered;
static atomic_uint ran, freed, dependencies;
static bool block_free;

static struct dma_fence *
test_dependency(struct drm_sched_job *job,struct drm_sched_entity *entity)
{
	CHECK(model_current==job->sched->thread && model_lock_depth==0);
	atomic_inc(&dependencies);
	return NULL;
}

static struct dma_fence *
test_run(struct drm_sched_job *base)
{
	struct test_job *job=container_of(base,struct test_job,base);
	CHECK(model_current==base->sched->thread && model_lock_depth==0);
	CHECK(atomic_load(&ran)==job->order);
	CHECK(atomic_read(&base->sched->hw_rq_count)==1);
	CHECK(!dma_fence_is_signaled(&base->s_fence->finished));
	atomic_inc(&ran);
	return job->error ? ERR_PTR(-job->error) : NULL;
}

static void
test_free(struct drm_sched_job *base)
{
	struct test_job *job=container_of(base,struct test_job,base);
	CHECK(model_current==base->sched->thread && model_lock_depth==0);
	CHECK(atomic_load(&freed)==job->order);
	CHECK(dma_fence_get_status(&base->s_fence->finished)==
	    (job->error ? -job->error : 1));
	CHECK(dma_fence_is_signaled(&base->s_fence->scheduled));
	if (block_free && job->order==0) {
		complete(&free_entered);
		wait_for_completion(&release_free);
	}
	drm_sched_job_cleanup(base);
	if (atomic_fetch_add(&freed,1)+1==4) complete(&drained);
}

static const struct drm_sched_backend_ops ops={
	.dependency=test_dependency, .run_job=test_run, .free_job=test_free,
};

static void
check_resources(void)
{
	CHECK(atomic_load(&model_slabs)==0);
	CHECK(atomic_load(&model_fences)==0);
	CHECK(atomic_load(&model_conditions)==0);
	CHECK(atomic_load(&model_locks)==0);
}

static void
test_serial_jobs(void)
{
	struct drm_gpu_scheduler sched={0}, *list[]={&sched};
	struct drm_sched_entity entity={0};
	struct test_job jobs[4]={0};
	struct dma_fence *outputs[4];
	unsigned i;
	CHECK(drm_sched_init(&sched,&ops,1,0,MAX_SCHEDULE_TIMEOUT,"contract")==0);
	CHECK(drm_sched_entity_init(&entity,DRM_SCHED_PRIORITY_NORMAL,list,1,NULL)==0);
	CHECK(drm_sched_entity_pop_job(&entity)==NULL);
	init_completion(&drained); init_completion(&release_free); init_completion(&free_entered);
	block_free=true;
	for (i=0;i<4;i++) {
		jobs[i].order=i;
		jobs[i].error=i==1 ? EIO : i==3 ? ECANCELED : 0;
		CHECK(drm_sched_job_init(&jobs[i].base,&entity,NULL)==0);
		outputs[i]=dma_fence_get(&jobs[i].base.s_fence->finished);
		CHECK(outputs[i]->seqno==i+1);
		drm_sched_entity_push_job(&jobs[i].base,&entity);
	}
	wait_for_completion(&free_entered);
	/* Cleanup may sleep. The scheduler must release its own locks first. */
	CHECK(atomic_load(&ran)==1 && atomic_load(&freed)==0);
	spin_lock(&sched.job_list_lock); spin_unlock(&sched.job_list_lock);
	spin_lock(&entity.rq_lock); spin_unlock(&entity.rq_lock);
	CHECK(spsc_queue_count(&entity.job_queue)==3);
	complete(&release_free);
	wait_for_completion(&drained);
	CHECK(atomic_load(&ran)==4 && atomic_load(&freed)==4);
	CHECK(atomic_load(&dependencies)==4);
	CHECK(spsc_queue_count(&entity.job_queue)==0);
	CHECK(atomic_read(&sched.hw_rq_count)==0);
	spin_lock(&sched.job_list_lock);
	CHECK(list_empty(&sched.ring_mirror_list));
	spin_unlock(&sched.job_list_lock);
	CHECK(drm_sched_entity_flush(&entity,MAX_SCHEDULE_TIMEOUT)==0);
	drm_sched_entity_fini(&entity);
	CHECK(atomic_load(&freed)==4);
	drm_sched_fini(&sched);
	for (i=0;i<4;i++) {
		CHECK(dma_fence_get_status(outputs[i])==(jobs[i].error ? -jobs[i].error : 1));
		dma_fence_put(outputs[i]);
	}
	destroy_completion(&drained); destroy_completion(&release_free); destroy_completion(&free_entered);
	check_resources();
}

static void
test_empty_and_allocation_failure(void)
{
	struct drm_gpu_scheduler sched={0}, *list[]={&sched};
	struct drm_sched_entity entity={0};
	struct drm_sched_job job={0};
	CHECK(drm_sched_init(&sched,&ops,1,0,MAX_SCHEDULE_TIMEOUT,"empty")==0);
	CHECK(drm_sched_entity_init(&entity,DRM_SCHED_PRIORITY_NORMAL,list,1,NULL)==0);
	model_fail_slab_alloc=true;
	CHECK(drm_sched_job_init(&job,&entity,NULL)==-ENOMEM);
	model_fail_slab_alloc=false;
	CHECK(job.s_fence==NULL && spsc_queue_count(&entity.job_queue)==0);
	CHECK(drm_sched_job_init(&job,&entity,NULL)==0);
	/* Preparation can unwind a job before publication and enqueue. */
	drm_sched_job_cleanup(&job);
	drm_sched_entity_fini(&entity);
	drm_sched_fini(&sched);
	check_resources();
}

static void
test_thread_failure(void)
{
	struct drm_gpu_scheduler sched={0};
	model_fail_kthread=true;
	CHECK(drm_sched_init(&sched,&ops,1,0,MAX_SCHEDULE_TIMEOUT,"failed")==-ENOMEM);
	model_fail_kthread=false;
	CHECK(sched.thread==NULL && !sched.ready);
	check_resources();
}

int
main(void)
{
	CHECK(drm_sched_fence_slab_init()==0);
	test_serial_jobs();
	test_empty_and_allocation_failure();
	test_thread_failure();
	drm_sched_fence_slab_fini();
	printf("DRM scheduler contract: ordered jobs, errors, sleeping cleanup, "
	    "drain, allocation failure, and thread failure passed (%u checks)\n",
	    atomic_load(&model_checks));
	return 0;
}
