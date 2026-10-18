/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Keep the original HSM structures and API over the shared clock engine. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display_hsm.h>
#include <arm/broadcom/bcm2712_display_clockvar.h>

static void
bcm2712_hsm_import(const struct bcm2712_hsm *sc, struct bcm2712_clock *clock)
{
	unsigned int i;

	/* The caller checked that this object is initialized. */
	memset(clock, 0, sizeof(*clock));
	clock->exchange = sc->exchange;
	clock->arg = sc->arg;
	for (i = 0; i < BCM2712_HSM_PORTS; i++)
		clock->status.minimum[i] = sc->status.minimum[i];
	clock->status.floor = sc->floor;
	clock->status.rate = sc->status.rate;
	clock->status.state = sc->status.state;
	clock->status.leases = sc->status.leases;
	clock->status.fault = sc->status.fault;
	clock->status.mutation_attempted = sc->status.mutation_attempted;
	clock->profile = BCM2712_CLOCK_HSM;
	clock->initial_state = sc->initial_state;
	clock->initialized = sc->initialized;
	clock->rate_changed = sc->rate_changed;
	clock->state_changed = sc->state_changed;
}

static void
bcm2712_hsm_export(const struct bcm2712_clock *clock, struct bcm2712_hsm *sc)
{
	unsigned int i;

	sc->exchange = clock->exchange;
	sc->arg = clock->arg;
	for (i = 0; i < BCM2712_HSM_PORTS; i++)
		sc->status.minimum[i] = clock->status.minimum[i];
	sc->floor = clock->status.floor;
	sc->status.rate = clock->status.rate;
	sc->status.state = clock->status.state;
	sc->status.leases = clock->status.leases;
	sc->status.fault = clock->status.fault;
	sc->status.mutation_attempted = clock->status.mutation_attempted;
	sc->initial_state = clock->initial_state;
	sc->initialized = clock->initialized;
	sc->rate_changed = clock->rate_changed;
	sc->state_changed = clock->state_changed;
}

int
bcm2712_hsm_init(struct bcm2712_hsm *out, bcm2712_hsm_exchange_fn exchange,
    void *arg)
{
	struct bcm2712_clock clock;
	struct bcm2712_hsm sc;
	int error;

	if (out == NULL)
		return EINVAL;
	error = bcm2712_clock_init(&clock, BCM2712_CLOCK_HSM, exchange, arg);
	if (error != 0)
		return error;
	memset(&sc, 0, sizeof(sc));
	bcm2712_hsm_export(&clock, &sc);
	*out = sc;
	return 0;
}

int
bcm2712_hsm_acquire(struct bcm2712_hsm *sc, unsigned int port, uint32_t minimum)
{
	struct bcm2712_clock clock;
	int error;

	/* Do not import other fields from an inactive object. */
	if (sc == NULL || !sc->initialized)
		return ENXIO;
	bcm2712_hsm_import(sc, &clock);
	error = bcm2712_clock_acquire(&clock, port, minimum);
	bcm2712_hsm_export(&clock, sc);
	return error;
}

int
bcm2712_hsm_update(struct bcm2712_hsm *sc, unsigned int port, uint32_t minimum)
{
	struct bcm2712_clock clock;
	int error;

	/* Do not import other fields from an inactive object. */
	if (sc == NULL || !sc->initialized)
		return ENXIO;
	bcm2712_hsm_import(sc, &clock);
	error = bcm2712_clock_update(&clock, port, minimum);
	bcm2712_hsm_export(&clock, sc);
	return error;
}

int
bcm2712_hsm_release(struct bcm2712_hsm *sc, unsigned int port)
{
	struct bcm2712_clock clock;
	int error;

	/* Do not import other fields from an inactive object. */
	if (sc == NULL || !sc->initialized)
		return ENXIO;
	bcm2712_hsm_import(sc, &clock);
	error = bcm2712_clock_release(&clock, port);
	bcm2712_hsm_export(&clock, sc);
	return error;
}

int
bcm2712_hsm_fini(struct bcm2712_hsm *sc)
{
	struct bcm2712_clock clock;
	int error;

	/* Do not import other fields from an inactive object. */
	if (sc == NULL || !sc->initialized)
		return ENXIO;
	bcm2712_hsm_import(sc, &clock);
	error = bcm2712_clock_fini(&clock);
	bcm2712_hsm_export(&clock, sc);
	return error;
}
