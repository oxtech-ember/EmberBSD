/* Origin: EmberBSD actual-source CL rejection contract, 2026-10-21. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define PAGE_SIZE 4096U
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define mstohz(n) (n)
typedef int kmutex_t;
typedef int spinlock_t;
typedef uint32_t __u32;
typedef uint64_t __u64;
#include "reject-types.h"
struct drm_gem_object { unsigned refs; };
struct bcmv3d_bo {
	struct drm_gem_object gem;
	uint32_t gpu;
	struct { size_t dmasize; } cma;
	unsigned busy, error;
};
struct dma_fence { unsigned refs; };
struct drm_syncobj { unsigned refs; };
struct bcmv3d_device { struct bcmv3d_job *job; unsigned dev; };
struct drm_device { struct bcmv3d_device *dev_private; };
struct drm_file { struct bcmv3d_bo *objects[V3D_CSD_BO_MAX]; };
struct bcmv3d_resv { bool locked, published; };
#include "reject-structs.h"

/* Helper failures are controlled boundaries. They do not claim that each
 * production helper currently returns EINVAL on a valid submission. */
enum failure { F_NONE, F_COPY, F_CAPTURE_BCL, F_CAPTURE_RCL, F_WAIT_BCL,
	F_WAIT_RCL, F_RESV, F_FENCE, F_PUBLISH, F_EXECUTE };
static struct {
	struct bcmv3d_job job;
	struct bcmv3d_device sc;
	struct drm_device dev;
	struct drm_file file;
	struct bcmv3d_bo objects[V3D_CSD_BO_MAX];
	uint32_t handles[V3D_CSD_BO_MAX];
	struct drm_v3d_submit_cl args;
	enum failure fail;
	int error;
	unsigned capture, waits, copies, executes, mmio, diagnoses, completes;
	unsigned fence_refs, sync_refs, receipts;
	bool reserved, resv_published, assert_released;
	char log[16][768];
} env;
static unsigned cas_interference;
static unsigned
atomic_load_relaxed(volatile unsigned *p)
{
	return __atomic_load_n(p, __ATOMIC_RELAXED);
}
static unsigned
atomic_cas_uint(volatile unsigned *p, unsigned old, unsigned value)
{
	if (cas_interference) {
		cas_interference--;
		__atomic_store_n(p, old + 1, __ATOMIC_RELAXED);
	}
	__atomic_compare_exchange_n(p, &old, value, false,
	    __ATOMIC_RELAXED, __ATOMIC_RELAXED);
	return old;
}
#define atomic_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
static void
mutex_enter(kmutex_t *lock)
{
	CHECK(*lock == 0); *lock = 1;
}
static void
mutex_exit(kmutex_t *lock)
{
	CHECK(*lock == 1); *lock = 0;
}
static int
copyin(const void *from, void *to, size_t size)
{
	env.copies++;
	if (env.fail == F_COPY) return env.error;
	CHECK(from == env.handles && size <= sizeof(env.handles));
	memcpy(to, from, size); return 0;
}
static struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *file, uint32_t handle)
{
	if (handle == 0 || handle > V3D_CSD_BO_MAX ||
	    file->objects[handle - 1] == NULL) return NULL;
	struct drm_gem_object *obj = &file->objects[handle - 1]->gem;
	obj->refs++; return obj;
}
static void
drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	CHECK(obj->refs > 1); obj->refs--;
}
static struct bcmv3d_bo *
bcmv3d_to_bo(struct drm_gem_object *obj)
{
	return (struct bcmv3d_bo *)obj;
}
static int
bcmv3d_sync_capture(struct drm_file *file, uint32_t in, uint32_t out,
    struct bcmv3d_sync *sync)
{
	static struct dma_fence input;
	static struct drm_syncobj output;
	(void)file; (void)in; (void)out;
	CHECK(env.job.lock == 0 && !env.reserved);
	env.capture++; sync->input = &input; env.fence_refs++;
	if (env.capture == 1) { sync->output = &output; env.sync_refs++; }
	return (env.fail == F_CAPTURE_BCL && env.capture == 1) ||
	    (env.fail == F_CAPTURE_RCL && env.capture == 2) ? env.error : 0;
}
static int
bcmv3d_sync_wait(struct bcmv3d_sync *sync)
{
	CHECK(sync->input != NULL && env.capture == 2);
	CHECK(env.job.lock == 0 && !env.reserved);
	env.waits++;
	return (env.fail == F_WAIT_BCL && env.waits == 1) ||
	    (env.fail == F_WAIT_RCL && env.waits == 2) ? env.error : 0;
}
static void
bcmv3d_sync_fini(struct bcmv3d_sync *sync)
{
	if (sync->input) { CHECK(env.fence_refs > 0); env.fence_refs--; }
	if (sync->fence) { CHECK(env.fence_refs > 0); env.fence_refs--; }
	if (sync->output) { CHECK(env.sync_refs > 0); env.sync_refs--; }
}
static int
bcmv3d_resv_prepare(struct bcmv3d_resv *resv,
    struct drm_gem_object **objects, unsigned count, int timeout)
{
	CHECK(env.job.lock == 0 && env.waits == 2 && timeout > 0);
	for (unsigned i = 0; i < count; i++) CHECK(objects[i]->refs == (env.job.poisoned ? 3U : 2U));
	resv->locked = true; env.reserved = true;
	return env.fail == F_RESV ? env.error : 0;
}
static void
bcmv3d_resv_unlock(struct bcmv3d_resv *resv)
{
	resv->locked = false; env.reserved = false;
}
static int
bcmv3d_sync_publish(struct bcmv3d_job *job, struct bcmv3d_sync *sync)
{
	static struct dma_fence fence;
	CHECK(job->lock == 1 && env.reserved && !job->poisoned);
	if (env.fail == F_FENCE) return env.error;
	sync->fence = &fence; env.fence_refs++; job->fence_seqno++; return 0;
}
static int
bcmv3d_resv_publish(struct bcmv3d_resv *resv, struct dma_fence *fence)
{
	CHECK(env.job.lock == 1 && resv->locked && fence != NULL);
	if (env.fail == F_PUBLISH) return env.error;
	resv->published = true; env.resv_published = true; return 0;
}
static void
bcmv3d_sync_complete(struct bcmv3d_sync *sync, int error)
{
	CHECK(env.job.lock == 1 && !env.reserved);
	(void)error; if (sync->fence) env.completes++;
}
static int
bcmv3d_cl_execute(struct bcmv3d_job *job, const struct drm_v3d_submit_cl *args)
{
	CHECK(job->lock == 1 && !env.reserved && env.resv_published);
	CHECK(args == &env.args);
	for (unsigned i = 0; i < job->nbo; i++) CHECK(job->bos[i]->busy == 1);
	env.executes++; return env.fail == F_EXECUTE ? env.error : 0;
}
static int bcmv3d_takeover_hub_poke(unsigned reg, unsigned value)
{ (void)reg; (void)value; env.mmio++; return 0; }
static int bcmv3d_takeover_core_poke(unsigned reg, unsigned value)
{ (void)reg; (void)value; env.mmio++; return 0; }
static void bcmv3d_mask(struct bcmv3d_job *job) { (void)job; env.mmio++; }
static void bcmv3d_core_mask(struct bcmv3d_job *job) { (void)job; env.mmio++; }
static void
bcmv3d_cl_diagnose(struct bcmv3d_device *sc, int error, unsigned count)
{
	CHECK(sc->job->poisoned && error != 0 && count == sc->job->nbo);
	env.diagnoses++;
}
static void
released(void)
{
	CHECK(env.job.lock == 0 && !env.reserved && !env.job.poisoned);
	CHECK(env.job.nbo == 0 && env.fence_refs == 0 && env.sync_refs == 0);
	for (unsigned i = 0; i < V3D_CSD_BO_MAX; i++) {
		CHECK(env.objects[i].gem.refs == 1);
		CHECK(env.objects[i].busy == 0 && env.objects[i].error == 0);
		CHECK(env.job.bos[i] == NULL);
	}
}
static void
aprint_error_dev(unsigned dev, const char *format, ...)
{
	CHECK(dev == env.sc.dev && env.receipts < __arraycount(env.log));
	if (env.assert_released) released();
	va_list args;
	va_start(args, format);
	int n = vsnprintf(env.log[env.receipts], sizeof(env.log[0]), format, args);
	va_end(args);
	CHECK(n > 0 && (size_t)n < sizeof(env.log[0])); env.receipts++;
}
#include "reject-body.h"

static void
setup(void)
{
	memset(&env, 0, sizeof(env));
	env.sc.job = &env.job; env.sc.dev = 17; env.dev.dev_private = &env.sc;
	env.error = EINVAL; env.assert_released = true;
	for (unsigned i = 0; i < V3D_CSD_BO_MAX; i++) {
		env.objects[i].gem.refs = 1;
		env.objects[i].gpu = 0x100000 + i * 0x10000;
		env.objects[i].cma.dmasize = 0x10000;
		env.file.objects[i] = &env.objects[i]; env.handles[i] = i + 1;
	}
	env.args = (struct drm_v3d_submit_cl){
	    .bcl_start = 0x100000, .bcl_end = 0x100040,
	    .rcl_start = 0x101000, .rcl_end = 0x101080,
	    .qma = 0x110000, .qms = 8192, .qts = 0x112000,
	    .bo_handles = (uintptr_t)env.handles, .bo_handle_count = 2,
	    .in_sync_bcl = 11, .in_sync_rcl = 12, .out_sync = 13};
}
static unsigned long
field(const char *name)
{
	char key[64], *end;
	snprintf(key, sizeof(key), "%s=", name);
	const char *value = strstr(env.log[0], key);
	CHECK(value != NULL); value += strlen(key);
	unsigned long number = strtoul(value, &end, 0);
	CHECK(end != value && (*end == ' ' || *end == '\n')); return number;
}
static void
expect(int error, const char *reason)
{
	CHECK(bcmv3d_submit_cl(&env.dev, &env.args, &env.file) == -error);
	CHECK(env.receipts == (reason != NULL ? 1U : 0U));
	CHECK(env.job.cl_rejections == env.receipts);
	if (reason) {
		char key[64]; snprintf(key, sizeof(key), "reason=%s ", reason);
		CHECK(strstr(env.log[0], key) != NULL);
		CHECK(field("errno") == (unsigned)error && field("sample") == 1);
		CHECK(field("bos") == env.args.bo_handle_count);
	}
	released(); CHECK(env.mmio == 0 && env.diagnoses == 0);
}
struct race { struct bcmv3d_job *job; uintptr_t bits; bool bad; };
static void *
race_take(void *arg)
{
	unsigned sample;
	struct race *race = arg;
	for (unsigned i = 0; i < 10000; i++)
		if (bcmv3d_cl_reject_take(race->job, &sample)) {
			if (sample < 1 || sample > 8) { race->bad = true; return NULL; }
			race->bits |= (uintptr_t)1 << (sample - 1);
		}
	return NULL;
}
int
main(void)
{
	unsigned sample;
	/* Every scalar is distinct. Neither user pointer is logged or followed. */
	setup(); env.args.flags = 0x8123; env.args.pad = 0x1234;
	env.args.perfmon_id = 321; env.args.extensions = UINT64_C(0xfeedabcddcba1234);
	env.args.bo_handles = UINT64_C(0xfeed543212345678);
	env.args.bo_handle_count = 65;
	expect(EINVAL, "header"); CHECK(env.copies == 0 && env.capture == 0);
	CHECK(field("flags") == env.args.flags && field("pad") == env.args.pad);
	CHECK(field("perfmon") == 321 && field("extensions-present") == 1);
	CHECK(field("bcl-start") == env.args.bcl_start && field("bcl-end") == env.args.bcl_end);
	CHECK(field("rcl-start") == env.args.rcl_start && field("rcl-end") == env.args.rcl_end);
	CHECK(field("qma") == env.args.qma && field("qms") == env.args.qms && field("qts") == env.args.qts);
	CHECK(field("in-bcl") == 11 && field("in-rcl") == 12 && field("out") == 13);
	CHECK(strstr(env.log[0], "feed") == NULL);
	for (unsigned n = 0; n < 13; n++) {
		setup();
		switch (n) {
		case 0: env.args.flags = 2; break;
		case 1: env.args.pad = 1; break;
		case 2: env.args.perfmon_id = 1; break;
		case 3: env.args.extensions = 1; break;
		case 4: env.args.bo_handle_count = 0; break;
		case 5: env.args.bo_handle_count = 65; break;
		case 6: env.args.rcl_end = 0; break;
		case 7: env.args.rcl_start = env.args.rcl_end; break;
		case 8: env.args.bcl_end = 0; break;
		case 9: env.args.qms = 0; break;
		case 10: env.args.qts++; break;
		case 11: env.args.qma++; break;
		case 12: env.args.qms++; break;
		}
		expect(EINVAL, "header"); CHECK(env.copies == 0);
	}
	setup(); env.handles[1] = env.handles[0]; expect(EINVAL, "duplicate");
	for (unsigned n = 0; n < 6; n++) {
		setup();
		switch (n) {
		case 0: env.args.rcl_start = 0; break;
		case 1: env.args.rcl_end = UINT32_MAX; break;
		case 2: env.args.bcl_start = 0; break;
		case 3: env.args.bcl_end = UINT32_MAX; break;
		case 4: env.args.qma = 0x300000; break;
		case 5: env.args.qts = 0x300000; break;
		}
		expect(EINVAL, "range"); CHECK(env.executes == 0);
	}
	const char *const reasons[] = {"sync-bcl", "sync-rcl", "wait-bcl", "wait-rcl",
	    "reservation", "fence-publish", "resv-publish"};
	for (unsigned n = 0; n < __arraycount(reasons); n++) {
		setup(); env.fail = (enum failure)(F_CAPTURE_BCL + n);
		expect(EINVAL, reasons[n]); CHECK(env.executes == 0);
		CHECK(env.completes == (env.fail == F_PUBLISH ? 1U : 0U));
	}
	/* Other errno values keep their sign and do not spend a receipt. */
	const int errors[] = {ENOMEM, ENOENT, ETIME, EINTR, EIO, EOVERFLOW};
	for (unsigned n = 0; n < __arraycount(errors); n++) {
		setup(); env.fail = F_WAIT_RCL; env.error = errors[n];
		expect(errors[n], NULL);
	}
	setup(); env.fail = F_COPY; env.error = EFAULT; expect(EFAULT, NULL);
	setup(); env.file.objects[1] = NULL; expect(ENOENT, NULL);
	setup(); expect(0, NULL); CHECK(env.executes == 1 && env.completes == 1);
	setup(); env.args.bo_handle_count = 64; expect(0, NULL);
	CHECK(env.executes == 1);
	/* The existing poison path retains every exposed reference and busy BO. */
	setup(); env.fail = F_EXECUTE; env.assert_released = false;
	CHECK(bcmv3d_submit_cl(&env.dev, &env.args, &env.file) == -EINVAL);
	CHECK(env.receipts == 0 && env.job.cl_rejections == 0);
	CHECK(env.job.poisoned && env.job.nbo == 2 && env.diagnoses == 1);
	CHECK(env.mmio == 4 && env.completes == 1 && env.fence_refs == 0);
	for (unsigned i = 0; i < 2; i++)
		CHECK(env.objects[i].gem.refs == 2 && env.objects[i].busy == 1 && env.objects[i].error == EIO);
	unsigned executions = env.executes;
	env.capture = env.waits = 0;
	CHECK(bcmv3d_submit_cl(&env.dev, &env.args, &env.file) == -EIO);
	CHECK(env.executes == executions && env.receipts == 0);
	/* Filter does not consume tickets for success, unrelated errors or NONE. */
	setup();
	bcmv3d_cl_reject(&env.sc, &env.args, BCMV3D_CL_REJECT_HEADER, 0);
	bcmv3d_cl_reject(&env.sc, &env.args, BCMV3D_CL_REJECT_HEADER, EIO);
	bcmv3d_cl_reject(&env.sc, &env.args, BCMV3D_CL_REJECT_HEADER, -EINVAL);
	bcmv3d_cl_reject(&env.sc, &env.args, BCMV3D_CL_REJECT_NONE, EINVAL);
	bcmv3d_cl_reject(&env.sc, &env.args, (enum bcmv3d_cl_reject_reason)99, EINVAL);
	CHECK(env.receipts == 0 && env.job.cl_rejections == 0);
	for (unsigned i = 0; i < 10000; i++)
		bcmv3d_cl_reject(&env.sc, &env.args, BCMV3D_CL_REJECT_HEADER, EINVAL);
	CHECK(env.receipts == 8 && env.job.cl_rejections == 8);
	for (unsigned i = 0; i < 8; i++) {
		char key[32]; snprintf(key, sizeof(key), "sample=%u ", i + 1);
		CHECK(strstr(env.log[i], key) != NULL);
	}
	struct bcmv3d_job second = {0};
	CHECK(bcmv3d_cl_reject_take(&second, &sample) && sample == 1);
	CHECK(env.job.cl_rejections == 8 && second.cl_rejections == 1);
	second.cl_rejections = UINT_MAX;
	CHECK(!bcmv3d_cl_reject_take(&second, &sample));
	CHECK(second.cl_rejections == UINT_MAX);
	/* A competing successful claim forces a retry; only our CAS owns a ticket. */
	second.cl_rejections = 0; cas_interference = 1;
	CHECK(bcmv3d_cl_reject_take(&second, &sample) && sample == 2);
	CHECK(second.cl_rejections == 2);
	second.cl_rejections = 7; cas_interference = 1;
	CHECK(!bcmv3d_cl_reject_take(&second, &sample));
	CHECK(second.cl_rejections == 8);
	second.cl_rejections = 0;
	pthread_t threads[8]; uintptr_t seen = 0;
	struct race races[8] = {0};
	for (unsigned i = 0; i < 8; i++) {
		races[i].job = &second;
		CHECK(pthread_create(&threads[i], NULL, race_take, &races[i]) == 0);
	}
	for (unsigned i = 0; i < 8; i++) {
		void *value;
		CHECK(pthread_join(threads[i], &value) == 0);
		uintptr_t bits = races[i].bits;
		CHECK(value == NULL && !races[i].bad && (seen & bits) == 0); seen |= bits;
	}
	CHECK(seen == 255 && second.cl_rejections == 8);
	printf("PASS: %lu CL rejection receipt/routing/lifetime checks; bounded CAS race\n", checks);
	return 0;
}
