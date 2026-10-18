/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD. Actual-source IRQ/sleep interleaving checks. */
#define BCMV3D_WAIT_CONTRACT
#include "bcm2712-v3d-cl-contract.c"

static bool waiter_enqueued, waiter_woken, irq_deferred, race_armed;
static unsigned wait_phases, phase_sleeps, lost_wakes;

static void
wait_complete(void)
{

	if (cl_model)
		cl_complete();
	else if (csd_model)
		csd_complete();
	else
		complete();
}

static void
wait_start(void)
{

	wait_phases++;
	phase_sleeps = 0;
	race_armed = model_case == 41;
}

static void
wait_read(const volatile unsigned *counter)
{
	struct bcmv3d_job *job = handler_arg;

	if (!race_armed || (counter != &job->events &&
	    counter != &job->core_events))
		return;
	/* The loaded value is already zero. An IRQ now races sleep enqueue. */
	race_armed = false;
	if (irq_mutex != NULL && *irq_mutex != 0)
		irq_deferred = true;
	else
		wait_complete();
}

static void
wait_unlock(const kmutex_t *lock)
{

	if (lock == irq_mutex && irq_deferred) {
		irq_deferred = false;
		wait_complete();
	}
}

static void
wait_wakeup(const void *channel)
{

	CHECK(channel == handler_arg);
	if (waiter_enqueued)
		waiter_woken = true;
	else
		lost_wakes++;
}

static int
wait_sleep(const void *channel, int priority, const char *name, int ticks,
    kmutex_t *lock)
{

	(void)priority;
	(void)name;
	CHECK(channel == handler_arg && ticks > 0 && ticks <= (int)mstohz(100));
	if (lock != NULL)
		CHECK(lock == irq_mutex && *lock == 1);
	waiter_enqueued = true;
	waiter_woken = false;
	sleeps++;
	phase_sleeps++;
	/* Real mtsleep enqueues before releasing the supplied mutex. */
	if (lock != NULL)
		mutex_exit(lock);
	if (model_case == 42 || (model_case == 43 && phase_sleeps > 2))
		wait_complete();
	if (model_case == 46) {
		/* Wrong parent/status must never become a successful completion. */
		regs[V3D_INT_STS/4] = csd_model ? V3D_TFUC : 0x30;
		handler(handler_arg);
	}
	if (model_case == 47) {
		fail_reg = reg_calls + 1;
		if (csd_model)
			core_handler(core_arg);
		else
			handler(handler_arg);
		fail_reg = 0;
	}
	if (model_case == 49 && phase_sleeps == 5) {
		time_ns += (uint64_t)ticks * (1000000000 / clock_hz);
		wait_complete();
	} else if (model_case == 43 || model_case == 45) {
		if (!waiter_woken)
			time_ns += 1000000000 / clock_hz;
	} else if (!waiter_woken) {
		time_ns += (uint64_t)ticks * (1000000000 / clock_hz);
	}
	waiter_enqueued = false;
	if (lock != NULL)
		mutex_enter(lock);
	return waiter_woken || model_case == 43 || model_case == 45 ?
	    0 : EWOULDBLOCK;
}

static void
wait_checks(unsigned engine, unsigned mode)
{
	struct bcmv3d_device sc;
	struct drm_device dev;
	struct drm_file file;
	struct drm_v3d_submit_tfu tfu = {0};
	struct drm_v3d_submit_csd csd = {0};
	struct drm_v3d_submit_cl cl = {0};
	uint32_t handles[2];
	uint64_t before, elapsed;
	unsigned phases = engine == 2 ? 2 : 1;
	bool poison = mode == 44 || mode == 45 || mode == 46 ||
	    mode == 47 || mode == 48;
	int error;

	irq_mutex = NULL;
	if (engine == 0)
		tfu = setup(&sc, &dev, &file);
	else if (engine == 1)
		csd = csd_setup(&sc, &dev, &file, handles);
	else
		cl = cl_setup(&sc, &dev, &file, handles);
	model_case = mode;
	wait_phases = phase_sleeps = lost_wakes = 0;
	waiter_enqueued = waiter_woken = irq_deferred = race_armed = false;
	on_event_read = wait_read;
	on_mutex_exit = wait_unlock;
	on_wakeup = wait_wakeup;
	on_engine_start = wait_start;
	on_sleep = wait_sleep;
	/* Unsigned tick subtraction must survive wrapping the tick counter. */
	if (mode == 48)
		time_ns = (uint64_t)(UINT32_MAX - 49) * (1000000000 / clock_hz);
	before = time_ns;
	if (engine == 0)
		error = bcmv3d_submit_tfu(&dev, &tfu, &file);
	else if (engine == 1)
		error = bcmv3d_submit_csd(&dev, &csd, &file);
	else
		error = bcmv3d_submit_cl(&dev, &cl, &file);
	elapsed = time_ns - before;
	CHECK(error == (poison ? -EIO : 0));
	CHECK(wait_phases == (poison ? 1 : phases));
	if (mode == 40)
		CHECK(sleeps == 0 && elapsed == 0);
	if (mode == 41 || mode == 42)
		CHECK(sleeps == phases && elapsed == 0 && lost_wakes == 0);
	if (mode == 43)
		CHECK(sleeps == 3 * phases && elapsed == (2000000000 / clock_hz) * phases);
	if (mode == 44 || mode == 48)
		CHECK(sleeps == 5 && elapsed == 500000000);
	if (mode == 45)
		CHECK(sleeps == mstohz(500) && elapsed == 500000000);
	if (mode == 46 || mode == 47)
		CHECK(sleeps == 1 && elapsed == 0);
	if (mode == 49)
		CHECK(sleeps == 5 * phases && elapsed == 500000000 * phases);
	CHECK(!irq_deferred && !waiter_enqueued);
	if (poison) {
		unsigned calls = reg_calls;
		struct drm_v3d_wait_bo wait = {
		    .handle = engine == 0 ? tfu.bo_handles[0] : handles[0]};

		CHECK(sc.job->poisoned);
		CHECK(bcmv3d_wait_bo(&dev, &wait, &file) == -EIO);
		if (engine == 0)
			CHECK(bcmv3d_submit_tfu(&dev, &tfu, &file) == -EIO);
		else if (engine == 1)
			CHECK(bcmv3d_submit_csd(&dev, &csd, &file) == -EIO);
		else
			CHECK(bcmv3d_submit_cl(&dev, &cl, &file) == -EIO);
		CHECK(reg_calls == calls);
	}
	on_event_read = NULL;
	on_mutex_exit = NULL;
	on_wakeup = NULL;
	on_engine_start = NULL;
	on_sleep = NULL;
	if (engine == 0)
		cleanup(&sc, &file, poison);
	else
		csd_cleanup(&sc, &file, poison);
}

int
main(void)
{

	const unsigned frequencies[] = {100, 1000};
	const char *selected = getenv("V3D_WAIT_ENGINE");
	unsigned first = selected != NULL ? (unsigned)strtoul(selected, NULL, 10) : 0;
	unsigned last = selected != NULL ? first + 1 : 3;

	CHECK(first < 3 && last <= 3);

	for (unsigned f = 0; f < __arraycount(frequencies); f++) {
		clock_hz = frequencies[f];
		for (unsigned engine = first; engine < last; engine++)
			for (unsigned mode = 40; mode <= 49; mode++)
				wait_checks(engine, mode);
	}
	printf("PASS: %u IRQ wait checks; TFU/CSD/CL, race, deadline and retention\n",
	    checks);
	return 0;
}
