/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Explicit GPU0 clock ownership. This provider never accesses display MMIO. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2712_display_hsm.h>
#include <arm/broadcom/bcm2835_mbox.h>
#include <arch/evbarm/rpi/vcio.h>

struct bcm2712_hsm_provider {
	struct bcm2712_hsm core;
};

/* Both HDMI controllers share firmware clock ID13. Only one owner may write. */
static ONCE_DECL(bcm2712_hsm_once);
static kmutex_t bcm2712_hsm_lock;
static struct bcm2712_hsm_provider *bcm2712_hsm_owner;

static int
bcm2712_hsm_once_init(void)
{
	mutex_init(&bcm2712_hsm_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
bcm2712_hsm_enter(void)
{
	int error;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	error = RUN_ONCE(&bcm2712_hsm_once, bcm2712_hsm_once_init);
	if (error != 0)
		return error;
	mutex_enter(&bcm2712_hsm_lock);
	return 0;
}

static int
bcm2712_hsm_exchange(void *arg, uint32_t *request, size_t length)
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
	error = bcm2712_hsm_enter();
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
	error = bcm2712_hsm_init(&sc->core, bcm2712_hsm_exchange, NULL);
	if (error != 0) {
		kmem_free(sc, sizeof(*sc));
		goto unlock;
	}
	bcm2712_hsm_owner = sc;
	*out = sc;
unlock:
	mutex_exit(&bcm2712_hsm_lock);
	return error;
}

static int
bcm2712_hsm_owned(struct bcm2712_hsm_provider *sc)
{
	int error = bcm2712_hsm_enter();

	if (error != 0)
		return error;
	if (sc == NULL || sc != bcm2712_hsm_owner) {
		mutex_exit(&bcm2712_hsm_lock);
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
	error = bcm2712_hsm_acquire(&sc->core, port, minimum);
	mutex_exit(&bcm2712_hsm_lock);
	return error;
}

int
bcm2712_display_hsm_update(struct bcm2712_hsm_provider *sc, unsigned int port,
    uint32_t minimum)
{
	int error = bcm2712_hsm_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_hsm_update(&sc->core, port, minimum);
	mutex_exit(&bcm2712_hsm_lock);
	return error;
}

int
bcm2712_display_hsm_release(struct bcm2712_hsm_provider *sc, unsigned int port)
{
	int error = bcm2712_hsm_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_hsm_release(&sc->core, port);
	mutex_exit(&bcm2712_hsm_lock);
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
	*out = sc->core.status;
	mutex_exit(&bcm2712_hsm_lock);
	return 0;
}

int
bcm2712_display_hsm_destroy(struct bcm2712_hsm_provider *sc)
{
	int error = bcm2712_hsm_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_hsm_fini(&sc->core);
	if (error == 0) {
		bcm2712_hsm_owner = NULL;
		kmem_free(sc, sizeof(*sc));
	}
	/* Keep ownership after an uncertain SET. No second writer may retry. */
	mutex_exit(&bcm2712_hsm_lock);
	return error;
}
