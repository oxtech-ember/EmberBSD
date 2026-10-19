/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD contributors (AI-assisted). */
/*
 * Actual intel_rps_init_early() and GT fini/release functions. The
 * test models intel_gt_pm_init_early() and the intel_gt_init() error
 * sequence. intel_gt_init_early()
 * creates the GT wakeref and the RPS locks before display setup.
 * Display and request boosting use them after a failed intel_gt_init(),
 * whose error path calls intel_gt_pm_fini(). intel_gt_driver_release()
 * calls it again. Only intel_gt_driver_late_release() may destroy the
 * locks, exactly once.
 */
#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(e) do { checks++; if (!(e)) { \
	fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #e); exit(1); \
} } while (0)

enum { UNUSED, LIVE, DESTROYED };
struct mutex { int state; int destroys; };
struct work_struct { void (*fn)(struct work_struct *); };
typedef struct { int counter; } atomic_t;

struct intel_rps {
	struct mutex lock;
	struct { struct mutex mutex; } power;
	struct work_struct work;
	atomic_t num_waiters;
};
struct intel_rc6 { int fini; };
struct intel_wakeref { struct mutex mutex; };
struct intel_uc { int late; };
struct i915_address_space { int refs; };
struct intel_gt {
	struct intel_rps rps;
	struct intel_rc6 rc6;
	struct intel_wakeref wakeref;
	struct intel_uc uc;
	struct i915_address_space *vm;
	int scratch_fini, late;
};

static void
mutex_init(struct mutex *m)
{
	CHECK(m->state != LIVE);
	m->state = LIVE;
}

static void
mutex_destroy(struct mutex *m)
{
	CHECK(m->state == LIVE);
	m->state = DESTROYED;
	m->destroys++;
}

/* The display path: intel_prepare_plane_fb -> intel_rps_mark_interactive. */
static void
use(struct mutex *m)
{
	CHECK(m->state == LIVE);
}

static void rps_work(struct work_struct *w) { (void)w; }
#define INIT_WORK(w, f) ((w)->fn = (f))
#define atomic_set(a, v) ((a)->counter = (v))
static struct i915_address_space *
fetch_vm(struct i915_address_space **p)
{
	struct i915_address_space *vm = *p;

	*p = NULL;
	return vm;
}
#define fetch_and_zero(p) fetch_vm(p)
static void i915_vm_put(struct i915_address_space *vm) { vm->refs--; }
static void intel_rc6_fini(struct intel_rc6 *r) { r->fini++; }
static void intel_wakeref_fini(struct intel_wakeref *w) { mutex_destroy(&w->mutex); }
static void intel_gt_fini_scratch(struct intel_gt *gt) { gt->scratch_fini++; }
static void intel_uc_driver_late_release(struct intel_uc *uc) { uc->late++; }
static void intel_gt_fini_requests(struct intel_gt *gt) { gt->late++; }
static void intel_gt_fini_reset(struct intel_gt *gt) { gt->late++; }
static void intel_gt_fini_timelines(struct intel_gt *gt) { gt->late++; }
static void intel_engines_free(struct intel_gt *gt) { gt->late++; }

void intel_rps_init_early(struct intel_rps *);
void intel_rps_fini(struct intel_rps *);
void intel_gt_pm_fini(struct intel_gt *);
void intel_gt_driver_release(struct intel_gt *);
void intel_gt_driver_late_release(struct intel_gt *);
#include "lifetime-body.h"

static void
init_early(struct intel_gt *gt)
{
	mutex_init(&gt->wakeref.mutex);	/* intel_gt_pm_init_early() */
	intel_rps_init_early(&gt->rps);
}

static void
use_rps(struct intel_gt *gt)
{
	use(&gt->wakeref.mutex);
	use(&gt->rps.lock);
	use(&gt->rps.power.mutex);
}

static void
check_destroyed_once(struct intel_gt *gt)
{
	CHECK(gt->wakeref.mutex.state == DESTROYED &&
	    gt->wakeref.mutex.destroys == 1);
	CHECK(gt->rps.lock.state == DESTROYED && gt->rps.lock.destroys == 1);
	CHECK(gt->rps.power.mutex.state == DESTROYED &&
	    gt->rps.power.mutex.destroys == 1);
}

int
main(void)
{
	struct i915_address_space vm = { .refs = 1 };
	struct intel_gt gt = { 0 };

	/* GT initialization fails; the display keeps the device attached. */
	init_early(&gt);
	CHECK(gt.rps.work.fn == rps_work && gt.rps.num_waiters.counter == 0);
	use_rps(&gt);
	intel_gt_pm_fini(&gt);		/* intel_gt_init() err_pm */
	CHECK(gt.rc6.fini == 1);
	use_rps(&gt);			/* modeset after the GT is wedged */
	intel_gt_driver_release(&gt);	/* i915_gem_driver_release() */
	use_rps(&gt);
	intel_gt_driver_late_release(&gt);
	CHECK(gt.late == 4 && gt.uc.late == 1);
	check_destroyed_once(&gt);

	/* Normal attach and detach. */
	gt = (struct intel_gt){ .vm = &vm };
	init_early(&gt);
	use_rps(&gt);
	intel_gt_driver_release(&gt);
	CHECK(gt.vm == NULL && vm.refs == 0 && gt.scratch_fini == 1);
	use_rps(&gt);
	intel_gt_driver_late_release(&gt);
	check_destroyed_once(&gt);

	/* Early probe failure: only i915_driver_early_probe() err_gem. */
	gt = (struct intel_gt){ 0 };
	init_early(&gt);
	intel_gt_driver_late_release(&gt);
	check_destroyed_once(&gt);

	printf("i915 GT wakeref and RPS lock lifetime contract: %u checks passed\n", checks);
	return 0;
}
