/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Explicit typed DISP16 owner. Selected only by its opt-in resident module. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2835_mbox.h>
#include <arch/evbarm/rpi/vcio.h>
#include "bcm2712_display_disp.h"
#include "bcm2712_display_dispvar.h"
#include "bcm2712_display_clockvar.h"

__CTASSERT(BCM2712_DISP_SLOTS == BCM2712_CLOCK_PORTS);
struct bcm2712_disp_provider {
	struct bcm2712_clock core;
};
static struct bcm2712_disp_provider bcm2712_disp_storage;
static struct bcm2712_disp_provider *bcm2712_disp_owner;
static bool bcm2712_disp_initialized;
static kmutex_t bcm2712_disp_lock;

/* The module serializes these hooks and quiesces every API caller first. */
int
bcm2712_disp_adapter_init(void)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (bcm2712_disp_initialized)
		return EBUSY;
	mutex_init(&bcm2712_disp_lock, MUTEX_DEFAULT, IPL_NONE);
	bcm2712_disp_initialized = true;
	return 0;
}

static int
bcm2712_disp_enter(void)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (!bcm2712_disp_initialized)
		return ENXIO;
	mutex_enter(&bcm2712_disp_lock);
	return 0;
}

int
bcm2712_disp_adapter_fini(void)
{
	int error = bcm2712_disp_enter();

	if (error != 0)
		return error;
	if (bcm2712_disp_owner != NULL) {
		mutex_exit(&bcm2712_disp_lock);
		return EBUSY;
	}
	mutex_exit(&bcm2712_disp_lock);
	/* Caller quiescence excludes waiters and a new entry during destruction. */
	bcm2712_disp_initialized = false;
	mutex_destroy(&bcm2712_disp_lock);
	return 0;
}

static int
bcm2712_disp_exchange(void *arg, uint32_t *request, size_t length)
{
	uint32_t response;

	(void)arg;
	return bcmmbox_request(BCMMBOX_CHANARM2VC, request, length, &response);
}

int
bcm2712_display_disp_create(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_disp_provider **out)
{
	struct bcm2712_display_contract contract;
	struct bcm2712_clock core;
	int error;

	if (dev == NULL || out == NULL)
		return EINVAL;
	error = bcm2712_disp_enter();
	if (error != 0)
		return error;
	if (bcm2712_disp_owner != NULL) {
		error = EBUSY;
		goto unlock;
	}
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		goto unlock;
	error = bcm2712_clock_init(&core, BCM2712_CLOCK_DISP,
	    bcm2712_disp_exchange, NULL);
	if (error != 0)
		goto unlock;
	bcm2712_disp_storage.core = core;
	bcm2712_disp_owner = &bcm2712_disp_storage;
	*out = bcm2712_disp_owner;
unlock:
	mutex_exit(&bcm2712_disp_lock);
	return error;
}

/* Validate identity before reading even one byte through the provider pointer. */
static int
bcm2712_disp_owned(const struct bcm2712_disp_provider *sc)
{
	int error = bcm2712_disp_enter();

	if (error != 0)
		return error;
	if (sc == NULL || sc != bcm2712_disp_owner) {
		mutex_exit(&bcm2712_disp_lock);
		return ENXIO;
	}
	return 0;
}

int
bcm2712_display_disp_acquire(struct bcm2712_disp_provider *sc, unsigned int slot,
    uint32_t minimum)
{
	int error = bcm2712_disp_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_acquire(&sc->core, slot, minimum);
	mutex_exit(&bcm2712_disp_lock);
	return error;
}

int
bcm2712_display_disp_update(struct bcm2712_disp_provider *sc, unsigned int slot,
    uint32_t minimum)
{
	int error = bcm2712_disp_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_update(&sc->core, slot, minimum);
	mutex_exit(&bcm2712_disp_lock);
	return error;
}

int
bcm2712_display_disp_release(struct bcm2712_disp_provider *sc, unsigned int slot)
{
	int error = bcm2712_disp_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_release(&sc->core, slot);
	mutex_exit(&bcm2712_disp_lock);
	return error;
}

int
bcm2712_display_disp_status(struct bcm2712_disp_provider *sc,
    struct bcm2712_disp_status *out)
{
	int error;

	if (out == NULL)
		return EINVAL;
	error = bcm2712_disp_owned(sc);
	if (error != 0)
		return error;
	memset(out, 0, sizeof(*out));
	memcpy(out->minimum, sc->core.status.minimum, sizeof(out->minimum));
	out->floor = sc->core.status.floor;
	out->state = sc->core.status.state;
	out->rate = sc->core.status.rate;
	out->leases = sc->core.status.leases;
	out->fault = sc->core.status.fault;
	out->mutation_attempted = sc->core.status.mutation_attempted;
	mutex_exit(&bcm2712_disp_lock);
	return 0;
}

int
bcm2712_display_disp_destroy(struct bcm2712_disp_provider *sc)
{
	int error = bcm2712_disp_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_fini(&sc->core);
	if (error == 0)
		bcm2712_disp_owner = NULL;
	/* A fault keeps this fixed slot and the resident callback alive. */
	mutex_exit(&bcm2712_disp_lock);
	return error;
}
