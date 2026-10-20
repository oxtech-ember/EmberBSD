/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual reservation caller and GEM lock helper; controlled native services. */
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#ifndef ETIME
#define ETIME 62
#endif
#ifndef ERESTARTSYS
#define ERESTARTSYS 512
#endif
static unsigned checks;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK %d: %s\n", __LINE__, #x); abort(); } checks++; } while (0)
#define KASSERT CHECK
#define EXPORT_SYMBOL(n)
struct ww_acquire_ctx { unsigned acquired; bool initialized, done, finished; };
static int reservation_ww_class;
struct dma_fence { unsigned refs; int status; };
struct dma_resv {
	struct ww_acquire_ctx *owner;
	struct dma_fence *exclusive, *shared[2];
};
struct drm_gem_object { struct dma_resv *resv; };
#include "resv-types.h"
#include "bcmv3d-kheapsort-model.h"
static struct drm_gem_object objects[64], *array[64];
static struct dma_resv reservations[64];
static struct dma_fence initial[64][3], producer;
static unsigned locks, unlocks, waits, snapshots, publications, frees, contexts, arrays;
static unsigned fault, fault_at, lock_calls;
static long budget;
static bool engine_locked, deadlock_injected;

void
ww_acquire_init(struct ww_acquire_ctx *ctx, int *class)
{
	CHECK(class == &reservation_ww_class && !engine_locked);
	memset(ctx, 0, sizeof(*ctx));
	ctx->initialized = true;
	contexts++;
}
void
ww_acquire_done(struct ww_acquire_ctx *ctx)
{
	CHECK(ctx->initialized && !ctx->finished && !ctx->done);
	ctx->done = true;
}
void
ww_acquire_fini(struct ww_acquire_ctx *ctx)
{
	CHECK(ctx->initialized && ctx->done && !ctx->finished && ctx->acquired == 0);
	ctx->finished = true;
	contexts--;
}
int
dma_resv_lock_interruptible(struct dma_resv *r, struct ww_acquire_ctx *ctx)
{
	CHECK(!engine_locked && r->owner == NULL && ctx->initialized && !ctx->done);
	lock_calls++;
	if (fault == 1 && lock_calls == fault_at)
		return -EINTR;
	if (fault == 2 && lock_calls == fault_at && !deadlock_injected) {
		deadlock_injected = true;
		return -EDEADLK;
	}
	r->owner = ctx; ctx->acquired++; locks++;
	return 0;
}
int
dma_resv_lock_slow_interruptible(struct dma_resv *r, struct ww_acquire_ctx *ctx)
{
	CHECK(deadlock_injected && ctx->acquired == 0);
	if (fault == 2 && fault_at == 64)
		return -EINTR;
	return dma_resv_lock_interruptible(r, ctx);
}
void
dma_resv_unlock(struct dma_resv *r)
{
	CHECK(r->owner != NULL && r->owner->acquired != 0);
	r->owner->acquired--; r->owner = NULL; unlocks++;
}
#include "gem-lock-body.h"

struct dma_fence *
dma_fence_get(struct dma_fence *f)
{
	if (f != NULL) { CHECK(f->refs > 0); f->refs++; }
	return f;
}
void
dma_fence_put(struct dma_fence *f)
{
	if (f != NULL) { CHECK(f->refs > 0); f->refs--; }
}
int
dma_resv_get_fences_rcu(const struct dma_resv *r, struct dma_fence **excl,
    unsigned *count, struct dma_fence ***shared)
{
	unsigned int index = 0, i;

	CHECK(r->owner != NULL && !engine_locked);
	if (fault == 3 && snapshots + 1 == fault_at)
		return -ENOMEM;
	*count = (r->shared[0] != NULL) + (r->shared[1] != NULL);
	*shared = *count == 0 ? NULL : malloc(*count * sizeof(**shared));
	CHECK(*count == 0 || *shared != NULL);
	if (*shared != NULL) arrays++;
	*excl = dma_fence_get(r->exclusive);
	for (i = 0; i < 2; i++) {
		if (r->shared[i] != NULL)
			(*shared)[index++] = dma_fence_get(r->shared[i]);
	}
	snapshots++;
	return 0;
}
void
kfree(void *p)
{
	if (p != NULL) frees++;
	free(p);
}
long
dma_fence_wait_timeout(struct dma_fence *f, bool intr, long timeout)
{
	CHECK(!engine_locked && intr && timeout > 0 && f->refs >= 3);
	CHECK(timeout == budget);
	waits++;
	if (fault == 4 && waits == fault_at) return 0;
	if (fault == 5 && waits == fault_at) return -EINTR;
	if (fault == 8 && waits == fault_at) return -ERESTARTSYS;
	if (fault == 6 && waits == fault_at) f->status = -EIO;
	else if (fault != 7 || waits != fault_at) f->status = 1;
	budget--;
	return budget;
}
int dma_fence_get_status(struct dma_fence *f) { CHECK(f != NULL && f->refs != 0); return f->status; }
void
dma_resv_add_excl_fence(struct dma_resv *r, struct dma_fence *f)
{
	CHECK(engine_locked && r->owner != NULL && r->owner->done && f == &producer);
	CHECK(f->status == 0);
	dma_fence_put(r->exclusive);
	dma_fence_put(r->shared[0]); dma_fence_put(r->shared[1]);
	r->exclusive = dma_fence_get(f);
	r->shared[0] = r->shared[1] = NULL;
	publications++;
}
bool dma_resv_held(struct dma_resv *r) { return r->owner != NULL; }
struct ww_acquire_ctx *dma_resv_locking_ctx(struct dma_resv *r) { return r->owner; }
#include "resv-body.h"

static void
reset(void)
{
	unsigned i, j;

	memset(reservations, 0, sizeof(reservations));
	for (i = 0; i < 64; i++) {
		objects[i].resv = &reservations[i]; array[i] = &objects[i];
		for (j = 0; j < 3; j++) {
			initial[i][j].refs = 2; initial[i][j].status = 0;
		}
		reservations[i].exclusive = &initial[i][0];
		reservations[i].shared[0] = &initial[i][1];
		reservations[i].shared[1] = &initial[i][2];
	}
	producer.refs = 1; producer.status = 0;
	locks = unlocks = waits = snapshots = publications = frees = contexts = lock_calls = arrays = 0;
	fault = fault_at = 0; budget = 1000;
	engine_locked = deadlock_injected = false;
}
static void
check_released(void)
{
	unsigned i, j;

	CHECK(locks == unlocks && contexts == 0 && frees == arrays);
	for (i = 0; i < 64; i++) {
		CHECK(reservations[i].owner == NULL);
		for (j = 0; j < 3; j++)
			CHECK(initial[i][j].refs == (publications != 0 ? 1U : 2U));
	}
}
int
main(void)
{
	struct bcmv3d_resv state, copied;
	unsigned int kind, at, mode;
	const char *control;
	int error;

	control = getenv("RESV_HARNESS_CONTROL");
	if (control != NULL) {
		if (strcmp(control, "segv") == 0) raise(SIGSEGV);
		if (strcmp(control, "abort") == 0) abort();
		if (strcmp(control, "hang") == 0) for (;;) {}
		CHECK(false);
	}
	reset(); memset(&state, 0, sizeof(state));
	CHECK(bcmv3d_resv_prepare(NULL, array, 64, 1000) == EINVAL);
	CHECK(bcmv3d_resv_prepare(&state, NULL, 64, 1000) == EINVAL);
	CHECK(bcmv3d_resv_prepare(&state, array, 0, 1000) == EINVAL);
	CHECK(bcmv3d_resv_prepare(&state, array, BCMV3D_RESV_MAX + 1, 1000) == EINVAL);
	CHECK(bcmv3d_resv_prepare(&state, array, 64, 0) == EINVAL);
	array[1] = NULL; CHECK(bcmv3d_resv_prepare(&state, array, 64, 1000) == EINVAL);
	array[1] = &objects[0]; CHECK(bcmv3d_resv_prepare(&state, array, 64, 1000) == EINVAL);
	array[1] = &objects[1]; objects[1].resv = objects[0].resv;
	CHECK(bcmv3d_resv_prepare(&state, array, 64, 1000) == EINVAL);
	CHECK(locks == 0 && contexts == 0);
	reset(); memset(&state, 0, sizeof(state));
	CHECK(bcmv3d_resv_prepare(&state, array, 64, 1000) == 0);
	CHECK(state.locked && waits == 192 && snapshots == 64 && publications == 0);
	CHECK(bcmv3d_resv_prepare(&state, array, 64, 1000) == EBUSY);
	CHECK(bcmv3d_resv_publish(&state, NULL) == EINVAL);
	producer.status = 1; CHECK(bcmv3d_resv_publish(&state, &producer) == EINVAL); producer.status = 0;
	engine_locked = true;
	copied = state;
	CHECK(bcmv3d_resv_publish(&copied, &producer) == EPERM && publications == 0);
	reservations[63].owner = &copied.acquire;
	CHECK(bcmv3d_resv_publish(&state, &producer) == EPERM && publications == 0);
	reservations[63].owner = &state.acquire;
	CHECK(bcmv3d_resv_publish(&state, &producer) == 0);
	CHECK(publications == 64 && producer.refs == 65);
	CHECK(bcmv3d_resv_publish(&state, &producer) == EALREADY);
	bcmv3d_resv_unlock(&state); bcmv3d_resv_unlock(&state);
	check_released();
	CHECK(!state.locked && state.objects == NULL && state.count == 0);
	CHECK(bcmv3d_resv_publish(&state, &producer) == EINVAL);
	/* A fresh BO has no prior fences. All three snapshot forms remain valid. */
	for (mode = 0; mode < 3; mode++) {
		reset(); memset(&state, 0, sizeof(state));
		if (mode != 1) { reservations[0].exclusive = NULL; initial[0][0].refs--; }
		if (mode != 2) {
			reservations[0].shared[0] = reservations[0].shared[1] = NULL;
			initial[0][1].refs--; initial[0][2].refs--;
		}
		CHECK(bcmv3d_resv_prepare(&state, array, 1, 1000) == 0);
		CHECK(waits == (mode == 0 ? 0U : mode == 1 ? 1U : 2U));
		bcmv3d_resv_unlock(&state);
		CHECK(locks == unlocks && contexts == 0 && frees == arrays);
	}
	for (kind = 1; kind <= 8; kind++) {
		for (at = 1; at <= (kind <= 3 ? 64U : 192U); at++) {
			reset(); memset(&state, 0, sizeof(state)); fault = kind; fault_at = at;
			error = bcmv3d_resv_prepare(&state, array, 64, 1000);
			if (kind == 2 && at != 64) {
				CHECK(error == 0 && deadlock_injected && state.locked);
				bcmv3d_resv_unlock(&state);
			} else {
				CHECK(error == (kind <= 2 || kind == 5 ? EINTR : kind == 3 ? ENOMEM : kind == 4 ? ETIME : kind == 8 ? ERESTARTSYS : EIO));
				CHECK(!state.locked && publications == 0);
			}
			check_released();
		}
	}
	/* Large private vectors use the same actual reservation owner. */
	static struct drm_gem_object many[BCMV3D_RESV_MAX];
	static struct drm_gem_object *list[BCMV3D_RESV_MAX];
	static struct dma_resv many_resv[BCMV3D_RESV_MAX];
	const unsigned sizes[] = {64, 65, 72, 91, BCMV3D_RESV_MAX};
	for (unsigned k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
		unsigned n = sizes[k];
		reset(); memset(&state, 0, sizeof(state));
		memset(many_resv, 0, sizeof(many_resv));
		for (unsigned i = 0; i < n; i++) {
			many[i].resv = &many_resv[i]; list[n - i - 1] = &many[i];
		}
		CHECK(bcmv3d_resv_prepare(&state, list, n, 1000) == 0);
		CHECK(state.count == n && snapshots == n && waits == 0);
		engine_locked = true;
		CHECK(bcmv3d_resv_publish(&state, &producer) == 0);
		CHECK(publications == n && producer.refs == n + 1);
		bcmv3d_resv_unlock(&state); engine_locked = false;
		for (unsigned i = 0; i < n; i++) {
			CHECK(many_resv[i].owner == NULL);
			dma_fence_put(many_resv[i].exclusive);
			many_resv[i].exclusive = NULL;
		}
		CHECK(producer.refs == 1 && locks == unlocks && contexts == 0);
		/* Nonadjacent aliases must be sorted before duplicate rejection. */
		list[n - 1] = list[0];
		CHECK(bcmv3d_resv_prepare(&state, list, n, 1000) == EINVAL);
		CHECK(!state.locked && locks == unlocks && contexts == 0);
	}
	printf("PASS actual BO resv caller/GEM locks: %u checks, 1152 fault positions\n", checks);
	return 0;
}
