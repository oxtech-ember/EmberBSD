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
#define KM_NOSLEEP 1
#define KM_SLEEP 0
#define CTASSERT(e) _Static_assert((e), #e)
#define KASSERT(e) CHECK(e)
#define BUS_DMASYNC_PREWRITE 1
#define V3D_PTE_VALID (1U << 28)
#define V3D_PTE_WRITE (1U << 29)
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define mstohz(n) (n)
typedef int kmutex_t;
typedef int spinlock_t;
typedef uint32_t __u32;
typedef uint64_t __u64;
#include "reject-types.h"
typedef struct { uint64_t ds_addr; size_t ds_len; } bus_dma_segment_t;
struct dma_map { int dm_nsegs; bus_dma_segment_t *dm_segs; };
struct drm_gem_object { unsigned refs; };
struct bcmv3d_bo {
	struct drm_gem_object gem;
	uint32_t gpu;
	struct { size_t dmasize; void *vaddr; struct dma_map *dmamap; } cma;
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
	struct drm_v3d_submit_csd csd_args;
	struct bcmv3d_bo pt;
	uint32_t ptes[1024 * 1024];
	struct dma_map maps[V3D_CSD_BO_MAX];
	bus_dma_segment_t segments[V3D_CSD_BO_MAX];
	bool csd;
	enum failure fail;
	int error;
	unsigned capture, waits, copies, executes, mmio, diagnoses, completes;
	unsigned fence_refs, sync_refs, receipts;
	unsigned alloc_calls, alloc_fail, alloc_live, lookups, maps_done, unmaps_done;
	size_t metadata_live, metadata_peak, last_object_free;
	struct { void *p; size_t bytes; bool objects; } metadata[4];
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
#define atomic_load_acquire(p) __atomic_load_n((p),__ATOMIC_ACQUIRE)
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
/* The list owner must allocate before lookup and free outside all locks. */
static struct bcmv3d_bo *bcmv3d_to_bo(struct drm_gem_object *);
static void *
kmem_zalloc(size_t size, int flags)
{
	CHECK(flags == KM_NOSLEEP && env.job.lock == 0 && !env.reserved);
	CHECK(size > 0 && size <= V3D_CSD_BO_MAX * sizeof(void *));
	env.alloc_calls++;
	if (env.alloc_calls == env.alloc_fail) return NULL;
	void *p = calloc(1, size); CHECK(p != NULL);
	for (unsigned i = 0; i < __arraycount(env.metadata); i++) {
		if (env.metadata[i].p != NULL) continue;
		env.metadata[i].p = p; env.metadata[i].bytes = size;
		env.metadata[i].objects = env.alloc_calls % 2 == 1;
		env.alloc_live++; env.metadata_live += size;
		if (env.metadata_live > env.metadata_peak) env.metadata_peak = env.metadata_live;
		return p;
	}
	CHECK(false); return NULL;
}
static void
kmem_free(void *p, size_t bytes)
{
	CHECK(env.job.lock == 0 && !env.reserved && p != NULL);
	for (unsigned i = 0; i < __arraycount(env.metadata); i++) {
		if (env.metadata[i].p != p) continue;
		CHECK(bytes == env.metadata[i].bytes && env.alloc_live > 0);
		if (env.metadata[i].objects) {
			struct drm_gem_object **objects = p;
			for (unsigned j = 0; j < bytes / sizeof(*objects); j++) {
				if (objects[j] == NULL) continue;
				unsigned index = (unsigned)(bcmv3d_to_bo(objects[j]) - env.objects);
				CHECK(index < V3D_CSD_BO_MAX);
				CHECK(objects[j]->refs ==
				    (env.job.poisoned && env.job.bos != NULL && index < env.job.nbo ? 2U : 1U));
			}
			env.last_object_free = bytes;
		}

		env.metadata[i].p = NULL; env.alloc_live--; env.metadata_live -= bytes;
		free(p); return;
	}
	CHECK(false);
}

static int
copyin(const void *from, void *to, size_t size)
{
	env.copies++;
	CHECK(env.alloc_live == (env.job.poisoned ? 3U : 2U));
	if (env.fail == F_COPY) return env.error;
	CHECK(from == env.handles && size <= sizeof(env.handles));
	memcpy(to, from, size); return 0;
}
static struct drm_gem_object *
drm_gem_object_lookup(struct drm_file *file, uint32_t handle)
{
	env.lookups++;
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
	CHECK(sync->input != NULL && env.capture == (env.csd ? 1U : 2U));
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
	CHECK(env.job.lock == 0 && env.waits == (env.csd ? 1U : 2U) && timeout > 0);
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
static void bcmv3d_ptes(struct bcmv3d_job *, bool);
static int
bcmv3d_cl_execute(struct bcmv3d_job *job, const struct drm_v3d_submit_cl *args)
{
	CHECK(job->lock == 1 && !env.reserved && env.resv_published);
	CHECK(args == &env.args);
	CHECK(job->nbo == env.args.bo_handle_count);
	for (unsigned i = 0; i < job->nbo; i++) CHECK(bcmv3d_to_bo(job->bos[i])->busy == 1);
		bcmv3d_ptes(job, true); env.maps_done++;
	for (unsigned i = 0; i < job->nbo; i++) {
		struct bcmv3d_bo *bo = bcmv3d_to_bo(job->bos[i]);
		for (unsigned p = 0; p < bo->cma.dmasize / PAGE_SIZE; p++)
			CHECK(env.ptes[(bo->gpu >> 12) + p] ==
			    (uint32_t)(env.segments[bo - env.objects].ds_addr / PAGE_SIZE + p) +
			    V3D_PTE_VALID + V3D_PTE_WRITE);
	}
	env.executes++;
	if (env.fail == F_EXECUTE) return env.error;
	bcmv3d_ptes(job, false); env.unmaps_done++;
	for (unsigned i = 0; i < job->nbo; i++) {
		struct bcmv3d_bo *bo = bcmv3d_to_bo(job->bos[i]);
		for (unsigned p = 0; p < bo->cma.dmasize / PAGE_SIZE; p++)
			CHECK(env.ptes[(bo->gpu >> 12) + p] == 0);
	}
	return 0;

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
	CHECK(env.job.nbo == 0 && env.job.bos == NULL && env.fence_refs == 0 && env.sync_refs == 0);
	CHECK(env.alloc_live == 0 && env.metadata_live == 0);
	for (unsigned i = 0; i < V3D_CSD_BO_MAX; i++) {
		CHECK(env.objects[i].gem.refs == 1);
		CHECK(env.objects[i].busy == 0 && env.objects[i].error == 0);
	}
}
static void
aprint_error_dev(unsigned dev, const char *format, ...)
{
	if (strncmp(format, "CL rejected:", 12) != 0) {
		CHECK(env.job.poisoned && env.job.lock == 1); env.diagnoses++; return;
	}
	CHECK(dev == env.sc.dev && env.receipts < __arraycount(env.log));
	if (env.assert_released) released();
	va_list args;
	va_start(args, format);
	int n = vsnprintf(env.log[env.receipts], sizeof(env.log[0]), format, args);
	va_end(args);
	CHECK(n > 0 && (size_t)n < sizeof(env.log[0])); env.receipts++;
}
/* Use the actual fixed-workspace native implementation, counting comparisons. */
#define kheapsort native_kheapsort
#define kheapsort_r native_kheapsort_r
#include "bcmv3d-kheapsort-model.h"
#undef kheapsort
#undef kheapsort_r
static unsigned sort_comparisons, sort_fail;
static int (*sort_compare)(const void *, const void *);
static int
count_compare(const void *a, const void *b)
{
	sort_comparisons++; return sort_compare(a, b);
}
static int
kheapsort(void *base, size_t count, size_t size,
    int (*compare)(const void *, const void *), void *temporary)
{
	CHECK(env.job.lock == 0 && !env.reserved);
	CHECK(size == sizeof(void *) && temporary != NULL);
	if (sort_fail) { sort_fail--; return -1; }
	sort_compare = compare;
	return native_kheapsort(base, count, size, count_compare, temporary);
}
static void bcmv3d_sync(struct bcmv3d_bo *bo, int flags)
{ CHECK(bo == &env.pt && flags == BUS_DMASYNC_PREWRITE); }
static void bcmv3d_ptes(struct bcmv3d_job *, bool);
static int bcmv3d_sync_prepare(struct drm_file *, uint32_t, uint32_t,
    struct bcmv3d_sync *);
static int bcmv3d_takeover_core_peek(unsigned reg, uint32_t *value)
{ CHECK(reg == V3D_CSD_STATUS); env.mmio++; *value = 0; return 0; }
static int
bcmv3d_csd_execute(struct bcmv3d_job *job, struct drm_v3d_submit_csd *args)
{
	CHECK(args == &env.csd_args); return bcmv3d_cl_execute(job, &env.args);
}
#include "reject-body.h"


static void
setup(void)
{
	CHECK(env.alloc_live == 0);
	memset(&env, 0, sizeof(env));
	sort_comparisons = sort_fail = 0;
	env.sc.job = &env.job; env.sc.dev = 17; env.dev.dev_private = &env.sc;
	env.error = EINVAL; env.assert_released = true;
	env.pt.cma.vaddr = env.ptes; env.job.pt = &env.pt;
	for (unsigned i = 0; i < V3D_CSD_BO_MAX; i++) {
		env.objects[i].gem.refs = 1;
		env.objects[i].gpu = 0x100000 + i * 0x10000;
		env.objects[i].cma.dmasize = 3 * PAGE_SIZE;
		env.segments[i] = (bus_dma_segment_t){0x20000000 + i * 0x10000, 3 * PAGE_SIZE};
		env.maps[i] = (struct dma_map){1, &env.segments[i]};
		env.objects[i].cma.dmamap = &env.maps[i];
		env.file.objects[i] = &env.objects[i]; env.handles[i] = i + 1;
	}
	env.args = (struct drm_v3d_submit_cl){
	    .bcl_start = 0x100000, .bcl_end = 0x100040,
	    .rcl_start = 0x101000, .rcl_end = 0x101080,
	    .qma = 0x110000, .qms = 8192, .qts = 0x112000,
	    .bo_handles = (uintptr_t)env.handles, .bo_handle_count = 2,
	    .in_sync_bcl = 11, .in_sync_rcl = 12, .out_sync = 13};
	env.csd_args = (struct drm_v3d_submit_csd){
	    .cfg = {0x10000,0x10000,0x10000,0x110,1,0x100003,0x101000},
	    .bo_handles = (uintptr_t)env.handles, .bo_handle_count = 2};
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
	CHECK((env.csd ? bcmv3d_submit_csd(&env.dev, &env.csd_args, &env.file) :
	    bcmv3d_submit_cl(&env.dev, &env.args, &env.file)) == -error);
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
static void
list_boundaries(void)
{
	const unsigned sizes[] = {64, 65, 72, 91, V3D_CSD_BO_MAX};
	for (unsigned engine = 0; engine < 2; engine++) {
		for (unsigned k = 0; k < __arraycount(sizes); k++) {
			unsigned n = sizes[k];
			setup(); env.csd = engine != 0;
			env.args.bo_handle_count = env.csd_args.bo_handle_count = n;
			/* Endpoints in the last BO prove that range checks use the full vector. */
			uint32_t last = env.objects[n - 1].gpu;
			env.args.rcl_start = last + 4096; env.args.rcl_end = last + 4224;
			env.args.qma = last; env.args.qts = last + 8192;
			env.csd_args.cfg[5] = last | 3U; env.csd_args.cfg[6] = last + 4096;
			/* Descending user order needs the actual native sort. */

			for (unsigned i = 0; i < n; i++) env.handles[i] = n - i;
			expect(0, NULL);
			CHECK(env.metadata_peak == n * (sizeof(void *) + sizeof(uint32_t)));
			CHECK(env.last_object_free == n * sizeof(void *));
			CHECK(sort_comparisons < 32U * n && env.lookups == n);
			CHECK(env.maps_done == 1 && env.unmaps_done == 1);
			/* Reuse after completion must not leave a stale borrowed vector. */
			env.capture = env.waits = 0;
			expect(0, NULL); CHECK(env.executes == 2 && env.maps_done == 2);
		}
		setup(); env.csd = engine != 0;
		env.args.bo_handle_count = env.csd_args.bo_handle_count = V3D_CSD_BO_MAX + 1;
		expect(EINVAL, engine ? NULL : "header");
		CHECK(env.alloc_calls == 0 && env.lookups == 0 && env.copies == 0);
		for (unsigned which = 1; which <= 2; which++) {
			setup(); env.csd = engine != 0; env.alloc_fail = which;
			env.args.bo_handle_count = env.csd_args.bo_handle_count = 91;
			expect(ENOMEM, NULL);
			CHECK(env.alloc_calls == which && env.copies == 0 && env.lookups == 0);
			CHECK(env.alloc_live == 0);
		}
		for (unsigned position = 0; position < 3; position++) {
			setup(); env.csd = engine != 0;
			env.args.bo_handle_count = env.csd_args.bo_handle_count = 91;
			unsigned at = position == 0 ? 0 : position == 1 ? 45 : 90;
			env.handles[at] = 0;
			expect(ENOENT, NULL);
			CHECK(env.lookups == at + 1 && env.last_object_free == 91 * sizeof(void *));
		}
		setup(); env.csd = engine != 0;
		env.args.bo_handle_count = env.csd_args.bo_handle_count = 91;
		env.handles[70] = env.handles[5]; env.handles[90] = 0;
		expect(EINVAL, engine ? NULL : "duplicate");
		CHECK(env.lookups == 91 && env.last_object_free == 91 * sizeof(void *));
		const enum failure failures[] = {F_CAPTURE_BCL, F_WAIT_BCL, F_RESV, F_FENCE, F_PUBLISH};
		const char *const where[] = {"sync-bcl", "wait-bcl", "reservation", "fence-publish", "resv-publish"};
		for (unsigned k = 0; k < __arraycount(failures); k++) {
			setup(); env.csd = engine != 0; env.fail = failures[k];
			env.args.bo_handle_count = env.csd_args.bo_handle_count = 91;
			expect(EINVAL, engine ? NULL : where[k]);
			CHECK(env.executes == 0 && env.last_object_free == 91 * sizeof(void *));
		}
		if (engine) {
			for (unsigned field = 5; field <= 6; field++) {
				setup(); env.csd = true; env.csd_args.cfg[field] = 0;
				expect(EINVAL, NULL); CHECK(env.executes == 0);
			}
		}
		setup(); env.csd = engine != 0; sort_fail = 1;

		expect(EIO, NULL); CHECK(env.capture == 0 && env.executes == 0);
		/* The old vector survives repeated and cross-engine poison rejection. */
		setup(); env.csd = engine != 0; env.fail = F_EXECUTE;
		env.args.bo_handle_count = env.csd_args.bo_handle_count = 91;
		env.assert_released = false;
		CHECK((engine ? bcmv3d_submit_csd(&env.dev, &env.csd_args, &env.file) :
		    bcmv3d_submit_cl(&env.dev, &env.args, &env.file)) == -EINVAL);
		struct drm_gem_object **saved = env.job.bos;
		CHECK(saved != NULL && env.alloc_live == 1 && env.job.nbo == 91);
		CHECK(env.maps_done == 1 && env.unmaps_done == 0 && env.job.poisoned);
		for (unsigned again = 0; again < 2; again++) {
			env.csd = again != 0; env.capture = env.waits = 0;
			CHECK((env.csd ? bcmv3d_submit_csd(&env.dev, &env.csd_args, &env.file) :
			    bcmv3d_submit_cl(&env.dev, &env.args, &env.file)) == -EIO);
			CHECK(env.job.bos == saved && env.job.nbo == 91 && env.alloc_live == 1);
			CHECK(env.executes == 1 && env.receipts == 0);
			for (unsigned i = 0; i < 91; i++)
				CHECK(saved[i]->refs == 2 && bcmv3d_to_bo(saved[i])->busy == 1 &&
				    bcmv3d_to_bo(saved[i])->error == EIO);
		}
		/* Model reboot only; production deliberately never takes this path. */
		for (unsigned i = 0; i < 91; i++) saved[i]->refs--;
		env.job.bos = NULL; env.job.nbo = 0;
		kmem_free(saved, 91 * sizeof(*saved));
	}
	setup();
}

int
main(void)
{
	unsigned sample;
	list_boundaries();
	/* Every scalar is distinct. Neither user pointer is logged or followed. */
	setup(); env.args.flags = 0x8123; env.args.pad = 0x1234;
	env.args.perfmon_id = 321; env.args.extensions = UINT64_C(0xfeedabcddcba1234);
	env.args.bo_handles = UINT64_C(0xfeed543212345678);
	env.args.bo_handle_count = V3D_CSD_BO_MAX + 1;
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
		case 5: env.args.bo_handle_count = V3D_CSD_BO_MAX + 1; break;
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
	struct drm_gem_object **retained = env.job.bos;
	CHECK(retained != NULL && env.alloc_live == 1);
	env.capture = env.waits = 0;
	CHECK(bcmv3d_submit_cl(&env.dev, &env.args, &env.file) == -EIO);
	CHECK(env.executes == executions && env.receipts == 0);
	CHECK(env.job.bos == retained && env.job.nbo == 2 && env.alloc_live == 1);
	for (unsigned i = 0; i < 2; i++) {
		CHECK(retained[i]->refs == 2); retained[i]->refs--;
		bcmv3d_to_bo(retained[i])->busy = bcmv3d_to_bo(retained[i])->error = 0;
	}
	env.job.bos = NULL; env.job.nbo = 0; env.job.poisoned = false;
	kmem_free(retained, 2 * sizeof(*retained));

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
