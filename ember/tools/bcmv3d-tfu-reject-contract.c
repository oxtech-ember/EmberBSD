/* Origin: EmberBSD actual-source TFU admission contract, 2026-10-21. */
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

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define KASSERT(e) CHECK(e)
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define mstohz(n) (n)
#define BUS_DMASYNC_PREWRITE 1
typedef int kmutex_t;
typedef int spinlock_t;
typedef uint32_t __u32;
typedef uint64_t __u64;
struct dma_fence { unsigned marker; };
struct drm_syncobj { unsigned marker; };
struct drm_gem_object { unsigned refs; };
struct bcmv3d_bo {
	struct drm_gem_object gem;
	uint32_t gpu;
	struct { uint64_t dmasize; } cma;
	unsigned busy, error;
};
#include "tfu-types.h"
struct bcmv3d_device { struct bcmv3d_job *job; unsigned dev; };
struct drm_device { struct bcmv3d_device *dev_private; };
struct drm_file { struct bcmv3d_bo *objects[5]; };
struct bcmv3d_resv { bool locked; };

/* Helper errors exercise the caller's routing and unwind. They do not claim
 * that each native helper can return every injected errno on valid input. */
enum failure { F_NONE, F_SYNC, F_RESV, F_FENCE, F_PUBLISH, F_EXECUTE };
static struct {
	struct bcmv3d_job job;
	struct bcmv3d_device sc;
	struct drm_device dev;
	struct drm_file file;
	struct bcmv3d_bo bo[4];
	struct drm_v3d_submit_tfu args;
	struct dma_fence input, fence;
	enum failure fail;
	int error;
	unsigned lookups, executes, completes, fences, mmio, diagnoses, masks;
	bool reserved, poison_metadata;
	unsigned receipts;
	char logs[BCMV3D_TFU_REJECT_MAX][1024];
} env;
static unsigned lose_cas;
static unsigned
atomic_load_relaxed(volatile unsigned *p)
{
	return __atomic_load_n(p, __ATOMIC_RELAXED);
}
static unsigned
atomic_cas_uint(volatile unsigned *p, unsigned old, unsigned value)
{
	if (__atomic_exchange_n(&lose_cas, 0, __ATOMIC_RELAXED) != 0)
		__atomic_store_n(p, old + 1, __ATOMIC_RELAXED);
	__atomic_compare_exchange_n(p, &old, value, false,
	    __ATOMIC_RELAXED, __ATOMIC_RELAXED);
	return old;
}
#define atomic_store_release(p,v) __atomic_store_n((p),(v),__ATOMIC_RELEASE)
static void mutex_enter(kmutex_t *p) { CHECK(*p == 0); *p = 1; }
static void mutex_exit(kmutex_t *p) { CHECK(*p == 1); *p = 0; }
static struct bcmv3d_bo *
bcmv3d_to_bo(struct drm_gem_object *obj)
{
	CHECK(obj != NULL && obj->refs >= 2);
	return (struct bcmv3d_bo *)obj;
}
/* Native GEM identity is exercised in the full job contract. */
static int
bcmv3d_bo_reference(struct bcmv3d_device *sc, struct drm_gem_object *obj,
    struct bcmv3d_bo **bo)
{
	CHECK(sc == &env.sc && obj->refs >= 2);
	obj->refs++; *bo = (struct bcmv3d_bo *)obj; return 0;
}
static struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *file, uint32_t handle)
{
	CHECK(env.job.lock == 0 && !env.reserved);
	env.lookups++;
	if (handle >= __arraycount(file->objects) || !file->objects[handle])
		return NULL;
	file->objects[handle]->gem.refs++;
	return &file->objects[handle]->gem;
}
static void
drm_gem_object_put_unlocked(struct drm_gem_object *obj)
{
	CHECK(env.job.lock == 0 && !env.reserved);
	CHECK(obj->refs >= 2); obj->refs--;
	if (env.poison_metadata && obj->refs == 1) {
		struct bcmv3d_bo *bo = (struct bcmv3d_bo *)obj;
		bo->gpu = 0xdead0000; bo->cma.dmasize = UINT64_MAX;
	}
}
static int
bcmv3d_sync_prepare(struct drm_file *file, uint32_t in, uint32_t out,
    struct bcmv3d_sync *sync)
{
	(void)file; (void)in; (void)out;
	CHECK(env.job.lock == 0 && !env.reserved);
	sync->input = &env.input; env.fences++;
	return env.fail == F_SYNC ? env.error : 0;
}
static void
bcmv3d_sync_fini(struct bcmv3d_sync *sync)
{
	if (sync->input) { CHECK(env.fences > 0); env.fences--; }
	if (sync->fence) { CHECK(env.fences > 0); env.fences--; }
	memset(sync, 0, sizeof(*sync));
}
static int
bcmv3d_sync_publish(struct bcmv3d_job *job, struct bcmv3d_sync *sync)
{
	CHECK(job->lock == 1 && env.reserved);
	sync->fence = &env.fence; env.fences++;
	return env.fail == F_FENCE ? env.error : 0;
}
static void
bcmv3d_sync_complete(struct bcmv3d_sync *sync, int error)
{
	(void)sync; (void)error;
	CHECK(env.job.lock == 1 && !env.reserved); env.completes++;
}
static int
bcmv3d_resv_prepare(struct bcmv3d_resv *r, struct drm_gem_object **objects,
    unsigned count, long ticks)
{
	CHECK(count == 2 && ticks > 0 && env.job.lock == 0);
	CHECK(objects[0] == &env.bo[0].gem && objects[1] == &env.bo[1].gem);
	r->locked = env.reserved = true;
	return env.fail == F_RESV ? env.error : 0;
}
static void
bcmv3d_resv_unlock(struct bcmv3d_resv *r)
{
	if (r->locked) { CHECK(env.reserved); r->locked = env.reserved = false; }
}
static int
bcmv3d_resv_publish(struct bcmv3d_resv *r, struct dma_fence *fence)
{
	CHECK(r->locked && fence != NULL && env.job.lock == 1);
	return env.fail == F_PUBLISH ? env.error : 0;
}
static int
bcmv3d_execute(struct bcmv3d_job *job, struct drm_v3d_submit_tfu *args)
{
	CHECK(job->lock == 1 && !env.reserved && args == &env.args);
	CHECK(job->src == &env.bo[1] && job->dst == &env.bo[0]);
	CHECK(job->src->busy == 1 && job->dst->busy == 1);
	CHECK(job->ntfu == 2 && job->tfu_bos[0] == &env.bo[0] && job->tfu_bos[1] == &env.bo[1]);
	env.executes++; return env.fail == F_EXECUTE ? env.error : 0;
}
static int
bcmv3d_takeover_hub_poke(uint32_t reg, uint32_t value)
{
	CHECK(env.fail == F_EXECUTE && reg == V3D_MSK_SET && value == V3D_IRQ_ALL);
	env.mmio++; return 0;
}
static void bcmv3d_mask(struct bcmv3d_job *job)
{ CHECK(job == &env.job && job->poisoned); env.masks++; }
static void bcmv3d_diagnose(struct bcmv3d_device *sc)
{ CHECK(sc == &env.sc && sc->job->poisoned); env.diagnoses++; }
static void
aprint_error_dev(unsigned dev, const char *fmt, ...)
{
	CHECK(dev == env.sc.dev);
	va_list ap; va_start(ap, fmt);
	if (strncmp(fmt, "TFU rejected:", 13) == 0) {
		CHECK(env.job.lock == 0 && !env.reserved && env.fences == 0);
		for (unsigned i = 0; i < __arraycount(env.bo); i++)
			CHECK(env.bo[i].gem.refs == 1);
		CHECK(env.receipts < BCMV3D_TFU_REJECT_MAX);
		int n = vsnprintf(env.logs[env.receipts], sizeof(env.logs[0]), fmt, ap);
		CHECK(n > 0 && (size_t)n < sizeof(env.logs[0])); env.receipts++;
	} else {
		CHECK(env.job.poisoned && env.fail == F_EXECUTE);
	}
	va_end(ap);
}
#include "tfu-body.h"

static void
setup(void)
{
	memset(&env, 0, sizeof(env));
	env.sc.job = &env.job; env.sc.dev = 71; env.dev.dev_private = &env.sc;
	for (unsigned i = 0; i < __arraycount(env.bo); i++) {
		env.bo[i].gem.refs = 1; env.bo[i].gpu = 0x1000000 + i * 0x10000;
		env.bo[i].cma.dmasize = 65536 + i * 4096;
		env.file.objects[i + 1] = &env.bo[i];
	}
	env.file.objects[0] = NULL;
	env.file.objects[1] = &env.bo[0]; env.file.objects[2] = &env.bo[1];
	env.args = (struct drm_v3d_submit_tfu) {
		.icfg = 0x001d0001, .iia = env.bo[1].gpu, .iis = 64,
		.ioa = env.bo[0].gpu, .ios = 0x00400040,
		.bo_handles = {1, 2}, .v71.ioc = 0x00400000
	};
	env.error = EINVAL;
}
static void
expected_receipt(const char *reason, int error, bool source, bool destination)
{
	unsigned count = 0;
	for (unsigned i = 0; i < __arraycount(env.args.bo_handles); i++)
		count += env.args.bo_handles[i] != 0;
	char expected[1024];
	const struct drm_v3d_submit_tfu *a = &env.args;
	int n = snprintf(expected, sizeof(expected),
	    "TFU rejected: sample=1 reason=%s errno=%d bos=%u flags=%#x pad=%#x "
	    "extensions-present=%u icfg=%#x iia=%#x ica=%#x iis=%#x iua=%#x "
	    "ioa=%#x ios=%#x ioc=%#x coef0=%#x coef1=%#x coef2=%#x coef3=%#x "
	    "in=%u out=%u src-known=%u src-bytes=%llu src-gpu=%#x "
	    "dst-known=%u dst-bytes=%llu dst-gpu=%#x\n",
	    reason, error, count, a->flags, a->v71.pad, (unsigned)(a->extensions != 0),
	    a->icfg, a->iia, a->ica, a->iis, a->iua, a->ioa, a->ios, a->v71.ioc,
	    a->coef[0], a->coef[1], a->coef[2], a->coef[3], a->in_sync, a->out_sync,
	    (unsigned)source, source ? 69632ULL : 0ULL, source ? 0x1010000 : 0,
	    (unsigned)destination, destination ? 65536ULL : 0ULL, destination ? 0x1000000 : 0);
	CHECK(n > 0 && (size_t)n < sizeof(expected));
	CHECK(env.receipts == 1 && strcmp(env.logs[0], expected) == 0);
}
static void
submit(int error, const char *reason, bool source, bool destination)
{
	CHECK(bcmv3d_submit_tfu(&env.dev, &env.args, &env.file) == -error);
	CHECK(env.job.lock == 0 && !env.reserved && env.fences == 0);
	if (reason) {
		expected_receipt(reason, error, source, destination);
		CHECK(env.job.tfu_rejections == 1 && env.executes == 0);
	} else {
		CHECK(env.receipts == 0 && env.job.tfu_rejections == 0);
	}
}
struct race { struct bcmv3d_job *job; unsigned count, seen[8]; };
static void *
race_claim(void *arg)
{
	struct race *race = arg;
	for (unsigned i = 0; i < 1000; i++) {
		unsigned sample;
		if (bcmv3d_tfu_reject_take(race->job, &sample)) {
			if (race->count >= __arraycount(race->seen)) abort();
			race->seen[race->count++] = sample;
		}
	}
	return NULL;
}
static void
counter_cases(void)
{
	struct bcmv3d_job a = {0}, b = {0}; unsigned sample = 99;
	a.cl_rejections = UINT_MAX;
	for (unsigned i = 1; i <= 8; i++) {
		CHECK(bcmv3d_tfu_reject_take(&a, &sample)); CHECK(sample == i);
	}
	for (unsigned i = 0; i < 100; i++) CHECK(!bcmv3d_tfu_reject_take(&a, &sample));
	CHECK(a.tfu_rejections == 8 && sample == 8 && a.cl_rejections == UINT_MAX);
	CHECK(bcmv3d_tfu_reject_take(&b, &sample) && sample == 1);
	a.tfu_rejections = UINT_MAX;
	CHECK(!bcmv3d_tfu_reject_take(&a, &sample) && a.tfu_rejections == UINT_MAX);
	a.tfu_rejections = 0; lose_cas = 1;
	CHECK(bcmv3d_tfu_reject_take(&a, &sample) && sample == 2);
	for (unsigned round = 0; round < 32; round++) {
		pthread_t threads[8]; struct race workers[8] = {0}; unsigned seen[9] = {0};
		a.tfu_rejections = 0;
		for (unsigned i = 0; i < 8; i++) {
			workers[i].job = &a;
			CHECK(pthread_create(&threads[i], NULL, race_claim, &workers[i]) == 0);
		}
		for (unsigned i = 0; i < 8; i++) {
			CHECK(pthread_join(threads[i], NULL) == 0);
			for (unsigned k = 0; k < workers[i].count; k++) {
				unsigned n = workers[i].seen[k]; CHECK(n >= 1 && n <= 8); seen[n]++;
			}
		}
		CHECK(a.tfu_rejections == 8);
		for (unsigned n = 1; n <= 8; n++) CHECK(seen[n] == 1);
	}
}
int
main(void)
{
	counter_cases();
	setup(); submit(0, NULL, true, true);
	CHECK(env.executes == 1 && env.bo[0].gem.refs == 1 && env.bo[1].gem.refs == 1);
	CHECK(!env.bo[0].busy && !env.bo[1].busy && !env.job.poisoned);
	setup(); env.args.bo_handles[0] = 0; submit(ENOENT, "dst-lookup", false, false); CHECK(env.lookups == 0);
	setup(); env.args.bo_handles[1] = 99; env.poison_metadata = true;
	submit(ENOENT, "src-lookup", false, true); CHECK(env.lookups == 2);
	setup(); env.args.bo_handles[0] = env.args.bo_handles[1] = 0;
	submit(ENOENT, "dst-lookup", false, false); CHECK(env.lookups == 0);
	setup(); env.args.bo_handles[1] = 99; submit(ENOENT, "src-lookup", false, true);
	/* Unsupported fields and address endpoints keep bounded receipts. */
	for (unsigned field = 0; field < 14; field++) {
		setup(); env.poison_metadata = true;
		switch (field) {
		case 0: env.args.icfg |= 2; break;
		case 1: env.args.iis = 0; break;
		case 2: env.args.ios = 0; break;
		case 3: env.args.v71.ioc = 0; break;
		case 4: env.args.ica = 17; break;
		case 5: env.args.iua = 19; break;
		case 6: case 7: case 8: case 9: env.args.coef[field - 6] = 11 + field; break;
		case 10: env.args.flags = UINT_MAX; break;
		case 11: env.args.extensions = UINT64_C(0xfedcba9800000000); break;
		case 12: env.args.iia = env.bo[1].gpu + (uint32_t)env.bo[1].cma.dmasize; break;
		case 13: env.args.ioa = env.bo[0].gpu + (uint32_t)env.bo[0].cma.dmasize; break;
		}
		submit(EINVAL, "payload", field != 12, true);
		CHECK(env.lookups == 2 && env.mmio == 0);
	}
	for (unsigned index = 2; index < 4; index++) {
		setup(); env.poison_metadata = true;
		if (index == 3) env.args.bo_handles[2] = 3;
		env.args.bo_handles[index] = 99;
		submit(ENOENT, "src-lookup", true, true);
		CHECK(env.lookups == index + 1 && env.mmio == 0);
	}
	setup(); env.args.bo_handles[3] = 3;
	submit(EINVAL, "payload", false, false); CHECK(env.lookups == 0);
	setup(); env.args.v71.pad = UINT_MAX; submit(EINVAL, "payload", true, true);
	/* One receipt gives every scalar a distinct value, and keeps extension
	 * presence separate from its deliberately invalid pointer-shaped value. */
	setup(); env.poison_metadata = true;
	env.args = (struct drm_v3d_submit_tfu) {.icfg=0x10203040,.iia=0x1010004,
	    .ica=0x22334455,.iis=0x33445566,.iua=0x44556677,.ioa=0x1000008,
	    .ios=0x66778899,.coef={0x10,0x20,0x30,0x40},.bo_handles={1,2,3,4},
	    .in_sync=77,.out_sync=88,.flags=0x1234,.extensions=UINT64_C(0xfedcba9800000000),
	    .v71={.ioc=0x778899aa,.pad=0x88776655}};
	submit(EINVAL, "payload", true, true); CHECK(env.lookups == 4);
	const enum failure failures[] = {F_SYNC, F_RESV, F_FENCE, F_PUBLISH};
	const char *const reasons[] = {"sync", "reservation", "fence-publish", "resv-publish"};
	const int errors[] = {EINVAL, ENOMEM, EFAULT, ENOENT, EINTR, ETIMEDOUT, EIO, EBUSY};
	for (unsigned f = 0; f < __arraycount(failures); f++) {
		for (unsigned e = 0; e < __arraycount(errors); e++) {
			setup(); env.fail = failures[f]; env.error = errors[e]; env.poison_metadata = true;
			submit(errors[e], reasons[f], true, true); CHECK(env.mmio == 0);
			CHECK(!env.job.poisoned && !env.bo[0].busy && !env.bo[1].busy);
		}
	}
	setup(); env.job.poisoned = true; env.job.src = &env.bo[2]; env.job.dst = &env.bo[3];
	submit(EIO, "poisoned", true, true);
	CHECK(env.job.src == &env.bo[2] && env.job.dst == &env.bo[3]);
	setup(); env.fail = F_EXECUTE; env.error = EINVAL; submit(EINVAL, NULL, true, true);
	CHECK(env.job.poisoned && env.executes == 1 && env.mmio == 1 && env.diagnoses == 1);
	CHECK(env.bo[0].gem.refs == 2 && env.bo[1].gem.refs == 2);
	CHECK(env.bo[0].busy && env.bo[1].busy && env.bo[0].error == EIO && env.bo[1].error == EIO);
	setup(); struct bcmv3d_tfu_reject_bo unknown = {0};
	for (unsigned reason = 0; reason <= 10; reason++) {
		bcmv3d_tfu_reject(&env.sc, &env.args, (enum bcmv3d_tfu_reject_reason)reason, 0, &unknown, &unknown);
	}
	bcmv3d_tfu_reject(&env.sc, &env.args, BCMV3D_TFU_REJECT_NONE, EINVAL, &unknown, &unknown);
	bcmv3d_tfu_reject(&env.sc, &env.args, (enum bcmv3d_tfu_reject_reason)-1, EINVAL, &unknown, &unknown);
	bcmv3d_tfu_reject(&env.sc, &env.args, (enum bcmv3d_tfu_reject_reason)99, EINVAL, &unknown, &unknown);
	CHECK(env.receipts == 0 && env.job.tfu_rejections == 0);
	for (unsigned i = 0; i < 100; i++)
		bcmv3d_tfu_reject(&env.sc, &env.args, BCMV3D_TFU_REJECT_PAYLOAD, EINVAL, &unknown, &unknown);
	CHECK(env.receipts == 8 && env.job.tfu_rejections == 8 && env.lookups == 0 && env.mmio == 0);
	printf("PASS: %u TFU admission receipt checks\n", checks);
	return 0;
}
