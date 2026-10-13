/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD tests of the production GPU render attach handlers. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define A733_GPU_EXPECTED UINT64_C(0x00240038006800b7)
typedef struct { bool held; } kmutex_t;
struct drm_device { unsigned int identity; };
struct sun60i_gpu_softc {
	kmutex_t sc_run_lock;
	struct drm_device *sc_render_device;
	bool sc_render_pending, sc_have_id;
	uint64_t sc_bvnc;
	void *sc_dev, *sc_dmat;
};
struct sysctlnode { void *sysctl_data; };
#define SYSCTLFN_ARGS const void *newp, const struct sysctlnode *rnode
#define SYSCTLFN_CALL(node) newp, (node)

static struct sun60i_gpu_softc *sun60i_gpu_instance;
static struct drm_device rendered = { 0x733 };
static bool tree_held, callback_reentry;
static unsigned int checks, calls, lookups, unlocks, relocks;
static int callback_error, lookup_error, read_value;
#define CHECK(e) do { \
	checks++; \
	if (!(e)) { \
		fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #e); \
		abort(); \
	} \
} while (0)

static int sun60i_gpu_render_attach(struct sun60i_gpu_softc *);
static int sun60i_gpu_sysctl_render_attach(SYSCTLFN_ARGS);

static void
mutex_enter(kmutex_t *lock)
{

	CHECK(!lock->held);
	lock->held = true;
}

static void
mutex_exit(kmutex_t *lock)
{

	CHECK(lock->held);
	lock->held = false;
}

static void
sysctl_unlock(void)
{

	CHECK(tree_held);
	CHECK(!sun60i_gpu_instance->sc_run_lock.held);
	tree_held = false;
	unlocks++;
}

static void
sysctl_relock(void)
{

	CHECK(!tree_held);
	CHECK(!sun60i_gpu_instance->sc_run_lock.held);
	tree_held = true;
	relocks++;
}

static int
sysctl_lookup(SYSCTLFN_ARGS)
{

	CHECK(tree_held);
	CHECK(!sun60i_gpu_instance->sc_run_lock.held);
	lookups++;
	read_value = *(int *)rnode->sysctl_data;
	if (lookup_error != 0)
		return lookup_error;
	if (newp != NULL)
		*(int *)rnode->sysctl_data = *(const int *)newp;
	return 0;
}

static int
pvr_render_attach(void *dev, void *dmat, uint64_t bvnc,
    struct drm_device **owner)
{
	struct sun60i_gpu_softc *sc = sun60i_gpu_instance;
	struct sysctlnode node = { 0 };
	int value = 1;

	calls++;
	CHECK(!tree_held && !sc->sc_run_lock.held);
	CHECK(sc->sc_render_pending && sc->sc_render_device == NULL);
	CHECK(dev == sc->sc_dev && dmat == sc->sc_dmat);
	CHECK(bvnc == A733_GPU_EXPECTED && owner != NULL && *owner == NULL);
	if (callback_reentry) {
		/* Simulate another sysctl writer while registration is in progress. */
		tree_held = true;
		CHECK(sun60i_gpu_sysctl_render_attach(&value, &node) == EBUSY);
		CHECK(tree_held && !sc->sc_run_lock.held);
		CHECK(sc->sc_render_pending && sc->sc_render_device == NULL);
		tree_held = false;
	}
	if (callback_error != 0)
		return callback_error;
	*owner = &rendered;
	return 0;
}

#include "pvr-attach-body.h"

static int
invoke(const int *value)
{
	struct sysctlnode node = { 0 };
	int error;

	CHECK(!tree_held);
	tree_held = true;
	error = sun60i_gpu_sysctl_render_attach(value, &node);
	CHECK(tree_held);
	if (sun60i_gpu_instance != NULL)
		CHECK(!sun60i_gpu_instance->sc_run_lock.held);
	tree_held = false;
	CHECK(unlocks == relocks);
	return error;
}

int
main(void)
{
	struct sun60i_gpu_softc sc;
	unsigned int saved_calls, saved_lookups;
	int value = 1;

	memset(&sc, 0, sizeof(sc));
	CHECK(invoke(&value) == ENXIO && lookups == 0 && calls == 0);
	sun60i_gpu_instance = &sc;
	sc.sc_dev = &sc;
	sc.sc_dmat = &rendered;
	CHECK(invoke(NULL) == 0 && read_value == 0);
	CHECK(invoke(&value) == ENODEV && calls == 0);
	sc.sc_have_id = true;
	CHECK(invoke(&value) == ENODEV && calls == 0);
	sc.sc_bvnc = A733_GPU_EXPECTED;
	for (value = -1; value <= 2; value++) {
		if (value == 1)
			continue;
		CHECK(invoke(&value) == EINVAL && calls == 0);
		CHECK(!sc.sc_render_pending && sc.sc_render_device == NULL);
	}
	value = 1;
	lookup_error = EFAULT;
	CHECK(invoke(&value) == EFAULT && calls == 0);
	lookup_error = 0;
	sc.sc_render_pending = true;
	CHECK(invoke(&value) == EBUSY && calls == 0);
	CHECK(sc.sc_render_pending && sc.sc_render_device == NULL);
	sc.sc_render_pending = false;
	callback_error = EIO;
	callback_reentry = true;
	CHECK(invoke(&value) == EIO && calls == 1);
	CHECK(!sc.sc_render_pending && sc.sc_render_device == NULL);
	callback_error = 0;
	CHECK(invoke(&value) == 0 && calls == 2);
	CHECK(!sc.sc_render_pending && sc.sc_render_device == &rendered);
	saved_calls = calls;
	saved_lookups = lookups;
	CHECK(invoke(&value) == 0 && calls == saved_calls);
	CHECK(lookups == saved_lookups + 1);
	CHECK(invoke(NULL) == 0 && read_value == 1 && calls == saved_calls);
	CHECK(sc.sc_render_device == &rendered);
	printf("PASS: %u PowerVR consumer attach checks\n", checks);
	return 0;
}
