/* Origin: EmberBSD; AI-assisted actual-source NPU initialization contract. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>

typedef unsigned int u_int;
#define KASSERT assert
#define FREAD 1
#define FWRITE 2
#define KAUTH_GENERIC_ISSUSER 1

enum {
	A733_NPU_STAGE_DOMAIN = 1,
	A733_NPU_STAGE_CLOCKS,
	A733_NPU_STAGE_PREFLIGHT,
	A733_NPU_STAGE_POWER_ID
};
struct lwp { int l_cred; };
struct a733_npu_tile { bool valid; };
struct sun60i_npu_softc {
	int sc_run_lock;
	u_int sc_run_stage, sc_run_result, sc_native_profile;
	bool sc_run_pass, sc_mmio_ready, sc_init_on_demand, sc_init_attempted;
	bool sc_tile_selected;
	int sc_task, sc_owned_clock, sc_owned_power, sc_task_result;
};
static int stage_error[5], stage_order[5], calls, preparations, runs, repeats;
static const int power_compat[] = { 1 };

static bool
mutex_owned(const int *lock)
{
	return *lock == 1;
}

static void
mutex_enter(int *lock)
{
	assert(*lock == 0);
	*lock = 1;
}

static void
mutex_exit(int *lock)
{
	assert(mutex_owned(lock));
	*lock = 0;
}

static int
kauth_authorize_generic(int cred, int action, void *unused)
{
	assert(action == KAUTH_GENERIC_ISSUSER && unused == NULL);
	return cred == 0 ? 0 : EPERM;
}

static bool
sun60i_a733_npu_task_tile_valid(const struct a733_npu_tile *tile)
{
	return tile->valid;
}

static int
sun60i_npu_power_node(struct sun60i_npu_softc *sc)
{
	assert(mutex_owned(&sc->sc_run_lock));
	return 7;
}

static bool
of_compatible_match(int node, const int *compat)
{
	return node == 7 && compat == power_compat;
}

static int
record_stage(const struct sun60i_npu_softc *sc, int stage)
{
	assert(mutex_owned(&sc->sc_run_lock));
	assert(sc->sc_run_stage + 1 == (u_int)stage);
	assert(calls < 5);
	stage_order[calls++] = stage;
	return stage_error[stage];
}

static int
sun60i_npu_stage_domain(struct sun60i_npu_softc *sc, int power)
{
	assert(power == 7);
	return record_stage(sc, A733_NPU_STAGE_DOMAIN);
}

static int
sun60i_npu_stage_clocks(struct sun60i_npu_softc *sc)
{
	return record_stage(sc, A733_NPU_STAGE_CLOCKS);
}

static int
sun60i_npu_stage_power_id(struct sun60i_npu_softc *sc)
{
	int error = record_stage(sc, A733_NPU_STAGE_POWER_ID);

	sc->sc_mmio_ready = error == 0;
	return error;
}

static int
sun60i_npu_task_prepare(struct sun60i_npu_softc *sc,
    const struct a733_npu_tile *tile)
{
	assert(mutex_owned(&sc->sc_run_lock) && tile->valid);
	preparations++;
	return sc->sc_mmio_ready && sc->sc_native_profile == 0 ? 0 : EBUSY;
}

static int
sun60i_npu_task_run(struct sun60i_npu_softc *sc)
{
	assert(mutex_owned(&sc->sc_run_lock) && sc->sc_tile_selected);
	runs++;
	return 0;
}

static int
sun60i_a733_npu_task_again(int task, int clk, int power,
    struct sun60i_npu_softc *sc, struct a733_npu_tile *tile)
{
	assert(mutex_owned(&sc->sc_run_lock) && tile->valid);
	assert(task == sc->sc_task && clk == sc->sc_owned_clock &&
	    power == sc->sc_owned_power);
	repeats++;
	return 0;
}

static int
sun60i_a733_npu_task_output(int task, struct a733_npu_tile *tile)
{
	assert(task == 0 && tile->valid);
	return 0;
}

static void
sun60i_npu_task_cache(struct sun60i_npu_softc *sc)
{
	assert(mutex_owned(&sc->sc_run_lock));
}

#include "npu-init.h"

int
main(void)
{
	const struct sun60i_npu_softc initial = {
		.sc_run_pass = true, .sc_init_on_demand = true
	};
	struct sun60i_npu_softc sc = initial;
	struct lwp root = { 0 }, other = { 1 };
	struct a733_npu_tile good = { true }, bad = { false };
	const int rw = FREAD | FWRITE;

	assert(sun60i_npu_tile_ioctl(&sc, &good, FREAD, &root) == EACCES);
	assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &other) == EPERM);
	assert(sun60i_npu_tile_ioctl(&sc, &bad, rw, &root) == EINVAL);
	assert(calls == 0 && preparations == 0 && !sc.sc_init_attempted);
	assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &root) == 0);
	assert(calls == 3 && stage_order[0] == 1 && stage_order[1] == 2 &&
	    stage_order[2] == 4 && preparations == 1 && runs == 1);
	assert(sc.sc_run_stage == 4 && sc.sc_run_result == 0);
	assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &root) == 0);
	assert(calls == 3 && preparations == 1 && runs == 1 && repeats == 1);

	for (int stage = 1; stage <= 4; stage++) {
		int error = stage == 1 ? EBUSY : EIO;

		if (stage == 3)
			continue;
		sc = initial;
		stage_error[stage] = error;
		calls = preparations = 0;
		assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &root) == error);
		assert(sc.sc_run_stage == (u_int)stage && !sc.sc_mmio_ready);
		assert(calls == (stage == 4 ? 3 : stage) && preparations == 0);
		assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &root) == EBUSY);
		assert(calls == (stage == 4 ? 3 : stage));
		stage_error[stage] = 0;
	}

	sc = initial;
	sc.sc_init_on_demand = false;
	calls = preparations = 0;
	assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &root) == EBUSY);
	assert(calls == 0 && preparations == 1 && !sc.sc_init_attempted);

	sc = initial;
	calls = preparations = 0;
	mutex_enter(&sc.sc_run_lock);
	assert(sun60i_npu_run_stage(&sc, 1) == 0);
	mutex_exit(&sc.sc_run_lock);
	assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &root) == EBUSY);
	assert(calls == 1 && preparations == 0 && !sc.sc_init_attempted);
	mutex_enter(&sc.sc_run_lock);
	assert(sun60i_npu_run_stage(&sc, 2) == 0);
	assert(sun60i_npu_run_stage(&sc, 3) == EBUSY);
	assert(sun60i_npu_run_stage(&sc, 4) == 0);
	mutex_exit(&sc.sc_run_lock);
	assert(sun60i_npu_tile_ioctl(&sc, &good, rw, &root) == 0);
	assert(calls == 3 && preparations == 1 && !sc.sc_init_attempted);
	puts("PASS: actual NPU stage dispatcher and typed tile authorization, "
	    "automatic and manual staging, early failures and repeat");
}
