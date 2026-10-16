/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Explicit GPU0 DVP ownership. No mapping, reset writes or boot attachment. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display_dvp.h>

struct bcm2712_dvp_provider {
	struct bcm2712_dvp core;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
};

static ONCE_DECL(bcm2712_dvp_once);
static kmutex_t bcm2712_dvp_lock;
static struct bcm2712_dvp_provider *bcm2712_dvp_owner;

static int
bcm2712_dvp_once_init(void)
{
	mutex_init(&bcm2712_dvp_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
bcm2712_dvp_enter(void)
{
	int error;

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	error = RUN_ONCE(&bcm2712_dvp_once, bcm2712_dvp_once_init);
	if (error != 0)
		return error;
	mutex_enter(&bcm2712_dvp_lock);
	return 0;
}

static int
bcm2712_dvp_bus_read(void *arg, unsigned int offset, uint32_t *out)
{
	struct bcm2712_dvp_provider *sc = arg;

	/* bus_space cannot recover a stalled transaction or synchronous abort. */
	*out = bus_space_read_4(sc->bst, sc->bsh, offset);
	return 0;
}

static int
bcm2712_dvp_bus_write(void *arg, unsigned int offset, uint32_t value)
{
	struct bcm2712_dvp_provider *sc = arg;

	bus_space_write_4(sc->bst, sc->bsh, offset, value);
	return 0;
}

static void
bcm2712_dvp_bus_barrier(void *arg)
{
	struct bcm2712_dvp_provider *sc = arg;

	bus_space_barrier(sc->bst, sc->bsh, 0, 0x10,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

int
bcm2712_display_dvp_create(device_t dev,
    const struct bcm2712_display_contract *contract,
    const struct bcm2712_display_memory *mapped, bus_space_tag_t bst,
    bus_space_handle_t bsh, struct bcm2712_dvp_provider **out)
{
	struct bcm2712_dvp_provider *sc;
	struct bcm2712_dvp_io io;
	int error;

	if (dev == NULL || out == NULL || bst == NULL)
		return EINVAL;
	error = bcm2712_dvp_enter();
	if (error != 0)
		return error;
	if (bcm2712_dvp_owner != NULL) {
		error = EBUSY;
		goto unlock;
	}
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	sc->bst = bst;
	sc->bsh = bsh;
	io = (struct bcm2712_dvp_io) {
		.read = bcm2712_dvp_bus_read, .write = bcm2712_dvp_bus_write,
		.barrier = bcm2712_dvp_bus_barrier, .arg = sc
	};
	error = bcm2712_dvp_init(&sc->core, contract, mapped, &io);
	if (error != 0) {
		kmem_free(sc, sizeof(*sc));
		goto unlock;
	}
	bcm2712_dvp_owner = sc;
	*out = sc;
unlock:
	mutex_exit(&bcm2712_dvp_lock);
	return error;
}

static int
bcm2712_dvp_owned(const struct bcm2712_dvp_provider *sc)
{
	int error = bcm2712_dvp_enter();

	if (error != 0)
		return error;
	if (sc == NULL || sc != bcm2712_dvp_owner) {
		mutex_exit(&bcm2712_dvp_lock);
		return ENXIO;
	}
	return 0;
}

int
bcm2712_display_dvp_acquire(struct bcm2712_dvp_provider *sc, unsigned int port)
{
	int error = bcm2712_dvp_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_dvp_acquire(&sc->core, port);
	mutex_exit(&bcm2712_dvp_lock);
	return error;
}

int
bcm2712_display_dvp_release(struct bcm2712_dvp_provider *sc, unsigned int port)
{
	int error = bcm2712_dvp_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_dvp_release(&sc->core, port);
	mutex_exit(&bcm2712_dvp_lock);
	return error;
}

int
bcm2712_display_dvp_status(struct bcm2712_dvp_provider *sc,
    struct bcm2712_dvp_status *out)
{
	int error;

	if (out == NULL)
		return EINVAL;
	error = bcm2712_dvp_owned(sc);
	if (error != 0)
		return error;
	*out = sc->core.status;
	mutex_exit(&bcm2712_dvp_lock);
	return 0;
}

int
bcm2712_display_dvp_destroy(struct bcm2712_dvp_provider *sc)
{
	int error = bcm2712_dvp_owned(sc);

	if (error != 0)
		return error;
	error = bcm2712_dvp_fini(&sc->core);
	if (error == 0) {
		bcm2712_dvp_owner = NULL;
		kmem_free(sc, sizeof(*sc));
	}
	/* Keep the owner and mapping after an uncertain write. */
	mutex_exit(&bcm2712_dvp_lock);
	return error;
}
