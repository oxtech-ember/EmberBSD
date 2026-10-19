/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Explicit GPU0 firmware-clock owners. No display MMIO or power operation. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2712_display_hsm.h>
#include <arm/broadcom/bcm2712_display_bvb.h>
#include <arm/broadcom/bcm2712_display_core.h>
#include <arm/broadcom/bcm2712_display_clockvar.h>
#include <arm/broadcom/bcm2835_mbox.h>
#include <arch/evbarm/rpi/vcio.h>

__CTASSERT(BCM2712_HSM_PORTS == BCM2712_CLOCK_PORTS);
__CTASSERT(BCM2712_BVB_PORTS == BCM2712_CLOCK_PORTS);
__CTASSERT(BCM2712_CORE_SLOTS == BCM2712_CLOCK_PORTS);

struct bcm2712_hsm_provider {
	struct bcm2712_clock core;
};

struct bcm2712_bvb_provider {
	struct bcm2712_clock core;
};

struct bcm2712_core_provider {
	struct bcm2712_clock core;
};

/* Fixed typed slots for firmware IDs4/13/14 share one transport lock. */
static ONCE_DECL(bcm2712_clock_once);
static kmutex_t bcm2712_clock_lock;
static struct bcm2712_hsm_provider *bcm2712_hsm_owner;
static struct bcm2712_bvb_provider *bcm2712_bvb_owner;
static struct bcm2712_core_provider *bcm2712_core_owner;

static int
bcm2712_clock_once_init(void)
{
	mutex_init(&bcm2712_clock_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
bcm2712_clock_enter(void)
{
	int error;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	error = RUN_ONCE(&bcm2712_clock_once, bcm2712_clock_once_init);
	if (error != 0)
		return error;
	mutex_enter(&bcm2712_clock_lock);
	return 0;
}

static int
bcm2712_clock_exchange(void *arg, uint32_t *request, size_t length)
{
	uint32_t response;

	(void)arg;
	return bcmmbox_request(BCMMBOX_CHANARM2VC, request, length, &response);
}

int
bcm2712_display_hsm_create(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_hsm_provider **out)
{
	struct bcm2712_display_contract contract;
	struct bcm2712_hsm_provider *sc;
	int error;

	if (dev == NULL || out == NULL)
		return EINVAL;
	error = bcm2712_clock_enter();
	if (error != 0)
		return error;
	if (bcm2712_hsm_owner != NULL) {
		error = EBUSY;
		goto unlock;
	}
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		goto unlock;
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	error = bcm2712_clock_init(&sc->core, BCM2712_CLOCK_HSM,
	    bcm2712_clock_exchange, NULL);
	if (error != 0) {
		kmem_free(sc, sizeof(*sc));
		goto unlock;
	}
	bcm2712_hsm_owner = sc;
	*out = sc;
unlock:
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

static int
bcm2712_hsm_owned(const struct bcm2712_hsm_provider *sc)
{
	int error = bcm2712_clock_enter();

	if (error != 0)
		return error;
	if (sc == NULL || sc != bcm2712_hsm_owner) {
		mutex_exit(&bcm2712_clock_lock);
		return ENXIO;
	}
	return 0;
}

int
bcm2712_display_hsm_acquire(struct bcm2712_hsm_provider *sc, unsigned int port,
    uint32_t minimum)
{
	int error = bcm2712_hsm_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_acquire(&sc->core, port, minimum);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_hsm_update(struct bcm2712_hsm_provider *sc, unsigned int port,
    uint32_t minimum)
{
	int error = bcm2712_hsm_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_update(&sc->core, port, minimum);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_hsm_release(struct bcm2712_hsm_provider *sc, unsigned int port)
{
	int error = bcm2712_hsm_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_release(&sc->core, port);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_hsm_status(struct bcm2712_hsm_provider *sc,
    struct bcm2712_hsm_status *out)
{
	int error;

	if (out == NULL)
		return EINVAL;
	error = bcm2712_hsm_owned(sc);
	if (error != 0)
		return error;
	memset(out, 0, sizeof(*out));
	memcpy(out->minimum, sc->core.status.minimum, sizeof(out->minimum));
	out->rate = sc->core.status.rate;
	out->state = sc->core.status.state;
	out->leases = sc->core.status.leases;
	out->fault = sc->core.status.fault;
	out->mutation_attempted = sc->core.status.mutation_attempted;
	mutex_exit(&bcm2712_clock_lock);
	return 0;
}

int
bcm2712_display_hsm_destroy(struct bcm2712_hsm_provider *sc)
{
	int error = bcm2712_hsm_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_fini(&sc->core);
	if (error == 0) {
		bcm2712_hsm_owner = NULL;
		kmem_free(sc, sizeof(*sc));
	}
	/* Keep ownership after an uncertain SET. No second writer may retry. */
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_bvb_create(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_bvb_provider **out)
{
	struct bcm2712_display_contract contract;
	struct bcm2712_bvb_provider *sc;
	int error;

	if (dev == NULL || out == NULL)
		return EINVAL;
	error = bcm2712_clock_enter();
	if (error != 0)
		return error;
	if (bcm2712_bvb_owner != NULL) {
		error = EBUSY;
		goto unlock;
	}
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		goto unlock;
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	error = bcm2712_clock_init(&sc->core, BCM2712_CLOCK_BVB,
	    bcm2712_clock_exchange, NULL);
	if (error != 0) {
		kmem_free(sc, sizeof(*sc));
		goto unlock;
	}
	bcm2712_bvb_owner = sc;
	*out = sc;
unlock:
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

static int
bcm2712_bvb_owned(const struct bcm2712_bvb_provider *sc)
{
	int error = bcm2712_clock_enter();

	if (error != 0)
		return error;
	if (sc == NULL || sc != bcm2712_bvb_owner) {
		mutex_exit(&bcm2712_clock_lock);
		return ENXIO;
	}
	return 0;
}

int
bcm2712_display_bvb_acquire(struct bcm2712_bvb_provider *sc, unsigned int port,
    uint32_t minimum)
{
	int error = bcm2712_bvb_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_acquire(&sc->core, port, minimum);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_bvb_update(struct bcm2712_bvb_provider *sc, unsigned int port,
    uint32_t minimum)
{
	int error = bcm2712_bvb_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_update(&sc->core, port, minimum);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_bvb_release(struct bcm2712_bvb_provider *sc, unsigned int port)
{
	int error = bcm2712_bvb_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_release(&sc->core, port);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_bvb_status(struct bcm2712_bvb_provider *sc,
    struct bcm2712_bvb_status *out)
{
	int error;

	if (out == NULL)
		return EINVAL;
	error = bcm2712_bvb_owned(sc);
	if (error != 0)
		return error;
	memset(out, 0, sizeof(*out));
	memcpy(out->minimum, sc->core.status.minimum, sizeof(out->minimum));
	out->floor = sc->core.status.floor;
	out->rate = sc->core.status.rate;
	out->state = sc->core.status.state;
	out->leases = sc->core.status.leases;
	out->fault = sc->core.status.fault;
	out->mutation_attempted = sc->core.status.mutation_attempted;
	mutex_exit(&bcm2712_clock_lock);
	return 0;
}

int
bcm2712_display_bvb_destroy(struct bcm2712_bvb_provider *sc)
{
	int error = bcm2712_bvb_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_fini(&sc->core);
	if (error == 0) {
		bcm2712_bvb_owner = NULL;
		kmem_free(sc, sizeof(*sc));
	}
	/* Keep ownership after an uncertain SET. No second writer may retry. */
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_core_create(device_t dev, ACPI_HANDLE handle,
    struct bcm2712_core_provider **out)
{
	struct bcm2712_display_contract contract;
	struct bcm2712_core_provider *sc;
	int error;

	if (dev == NULL || out == NULL)
		return EINVAL;
	error = bcm2712_clock_enter();
	if (error != 0)
		return error;
	if (bcm2712_core_owner != NULL) {
		error = EBUSY;
		goto unlock;
	}
	error = bcm2712_display_acpi_resources(dev, handle, &contract);
	if (error != 0)
		goto unlock;
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	error = bcm2712_clock_init(&sc->core, BCM2712_CLOCK_CORE,
	    bcm2712_clock_exchange, NULL);
	if (error != 0) {
		kmem_free(sc, sizeof(*sc));
		goto unlock;
	}
	bcm2712_core_owner = sc;
	*out = sc;
unlock:
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

static int
bcm2712_core_owned(const struct bcm2712_core_provider *sc)
{
	int error = bcm2712_clock_enter();

	if (error != 0)
		return error;
	if (sc == NULL || sc != bcm2712_core_owner) {
		mutex_exit(&bcm2712_clock_lock);
		return ENXIO;
	}
	return 0;
}

int
bcm2712_display_core_acquire(struct bcm2712_core_provider *sc, unsigned int slot,
    uint32_t minimum)
{
	int error = bcm2712_core_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_acquire(&sc->core, slot, minimum);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_core_update(struct bcm2712_core_provider *sc, unsigned int slot,
    uint32_t minimum)
{
	int error = bcm2712_core_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_update(&sc->core, slot, minimum);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_core_release(struct bcm2712_core_provider *sc, unsigned int slot)
{
	int error = bcm2712_core_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_release(&sc->core, slot);
	mutex_exit(&bcm2712_clock_lock);
	return error;
}

int
bcm2712_display_core_status(struct bcm2712_core_provider *sc,
    struct bcm2712_core_status *out)
{
	int error;

	if (out == NULL)
		return EINVAL;
	error = bcm2712_core_owned(sc);
	if (error != 0)
		return error;
	memset(out, 0, sizeof(*out));
	memcpy(out->minimum, sc->core.status.minimum, sizeof(out->minimum));
	out->floor = sc->core.status.floor;
	out->rate = sc->core.status.rate;
	out->state = sc->core.status.state;
	out->leases = sc->core.status.leases;
	out->fault = sc->core.status.fault;
	out->mutation_attempted = sc->core.status.mutation_attempted;
	mutex_exit(&bcm2712_clock_lock);
	return 0;
}

int
bcm2712_display_core_destroy(struct bcm2712_core_provider *sc)
{
	int error = bcm2712_core_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_clock_fini(&sc->core);
	if (error == 0) {
		bcm2712_core_owner = NULL;
		kmem_free(sc, sizeof(*sc));
	}
	/* Keep ownership after an uncertain SET. No second writer may retry. */
	mutex_exit(&bcm2712_clock_lock);
	return error;
}
