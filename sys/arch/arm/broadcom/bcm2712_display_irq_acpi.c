/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Explicit lifecycle API only. No automatic boot attachment. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/device.h>
#include <sys/kmem.h>
#include <sys/lwp.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/workqueue.h>
#include <sys/xcall.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_intr.h>
#include <arm/broadcom/bcm2712_display_irq.h>

struct bcm2712_l2_provider {
	struct bcm2712_l2 core;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	bus_size_t length;
	kmutex_t lock;
	void *ih;
	struct workqueue *wq;
	struct work work;
	struct lwp *worker;
	bcm2712_display_irq_fn service;
	void *arg;
	bool stopping, queued;
};

static uint32_t
bcm2712_irq_read(void *arg, unsigned int offset)
{
	struct bcm2712_l2_provider *sc = arg;

	return bus_space_read_4(sc->bst, sc->bsh, offset);
}

static void
bcm2712_irq_write(void *arg, unsigned int offset, uint32_t value)
{
	struct bcm2712_l2_provider *sc = arg;

	bus_space_write_4(sc->bst, sc->bsh, offset, value);
}

static void
bcm2712_irq_barrier(void *arg)
{
	struct bcm2712_l2_provider *sc = arg;

	bus_space_barrier(sc->bst, sc->bsh, 0, sc->length,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static void
bcm2712_irq_work(struct work *work, void *arg)
{
	struct bcm2712_l2_provider *sc = arg;
	uint32_t pending;
	int error, result;

	(void)work;
	mutex_enter(&sc->lock);
	sc->queued = false;
	if (sc->stopping) {
		mutex_exit(&sc->lock);
		return;
	}
	pending = bcm2712_l2_take(&sc->core);
	error = sc->core.fault;
	sc->worker = curlwp;
	mutex_exit(&sc->lock);
	/* The owner clears a BSC level at its peripheral before returning zero. */
	result = sc->service(sc->arg, pending, error);
	mutex_enter(&sc->lock);
	sc->worker = NULL;
	if (!sc->stopping) {
		bcm2712_l2_complete(&sc->core, pending, result);
		if (sc->core.fault != 0)
			acpi_intr_mask(sc->ih);
	}
	mutex_exit(&sc->lock);
}

static int
bcm2712_irq_intr(void *arg)
{
	struct bcm2712_l2_provider *sc = arg;
	uint32_t pending;
	int error;

	mutex_enter(&sc->lock);
	if (sc->stopping) {
		mutex_exit(&sc->lock);
		return 0;
	}
	error = bcm2712_l2_collect(&sc->core, &pending);
	if (error != 0)
		acpi_intr_mask(sc->ih);
	if ((pending != 0 || error != 0) && !sc->queued) {
		sc->queued = true;
		workqueue_enqueue(sc->wq, &sc->work, NULL);
	}
	mutex_exit(&sc->lock);
	return pending != 0 || error != 0;
}

int
bcm2712_display_irq_create(device_t dev,
    const struct bcm2712_display_contract *contract, enum bcm2712_l2_domain domain,
    const struct bcm2712_display_memory *mapped, bus_space_tag_t bst,
    bus_space_handle_t bsh, uint32_t owned, bcm2712_display_irq_fn service,
    void *arg, struct bcm2712_l2_provider **out)
{
	struct bcm2712_l2_provider *sc;
	struct bcm2712_l2_io io;
	struct acpi_irq irq;
	int error;

	ASSERT_SLEEPABLE();
	if (dev == NULL || bst == NULL || service == NULL || out == NULL ||
	    mapped == NULL)
		return EINVAL;
	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);
	sc->bst = bst;
	sc->bsh = bsh;
	sc->length = mapped->length;
	sc->service = service;
	sc->arg = arg;
	io = (struct bcm2712_l2_io) {
		.read = bcm2712_irq_read, .write = bcm2712_irq_write,
		.barrier = bcm2712_irq_barrier, .arg = sc
	};
	error = bcm2712_l2_init(&sc->core, contract, domain, mapped, owned, &io);
	if (error != 0)
		goto free;
	mutex_init(&sc->lock, MUTEX_DEFAULT, IPL_VM);
	error = workqueue_create(&sc->wq, "bcm2712l2", bcm2712_irq_work, sc,
	    PRI_NONE, IPL_VM, WQ_MPSAFE);
	if (error != 0)
		goto destroy_lock;
	error = bcm2712_l2_prepare(&sc->core);
	if (error != 0)
		goto destroy_queue;
	memset(&irq, 0, sizeof(irq));
	irq.ar_irq = contract->irq[bcm2712_display_domains[domain].irq_index];
	irq.ar_type = ACPI_LEVEL_SENSITIVE;
	sc->ih = acpi_intr_establish_irq(dev, &irq, IPL_VM, true,
	    bcm2712_irq_intr, sc, bcm2712_display_domains[domain].name);
	if (sc->ih == NULL) {
		error = ENXIO;
		bcm2712_l2_rollback(&sc->core);
		goto destroy_queue;
	}
	mutex_enter(&sc->lock);
	error = bcm2712_l2_arm(&sc->core);
	mutex_exit(&sc->lock);
	KASSERT(error == 0);
	*out = sc;
	return 0;

destroy_queue:
	workqueue_destroy(sc->wq);
destroy_lock:
	mutex_destroy(&sc->lock);
free:
	kmem_free(sc, sizeof(*sc));
	return error;
}

int
bcm2712_display_irq_destroy(struct bcm2712_l2_provider *sc)
{
	ASSERT_SLEEPABLE();
	if (sc == NULL)
		return EINVAL;
	mutex_enter(&sc->lock);
	if (sc->worker == curlwp) {
		mutex_exit(&sc->lock);
		return EDEADLK;
	}
	sc->stopping = true;
	bcm2712_l2_stop(&sc->core, 0);
	mutex_exit(&sc->lock);
	/* Stop new IRQ work before waiting. Never wait while holding IPL_VM. */
	acpi_intr_mask(sc->ih);
	xc_barrier(0);
	acpi_intr_disestablish(sc->ih);
	workqueue_wait(sc->wq, &sc->work);
	workqueue_destroy(sc->wq);
	mutex_destroy(&sc->lock);
	kmem_free(sc, sizeof(*sc));
	return 0;
}

int
bcm2712_display_irq_error(struct bcm2712_l2_provider *sc)
{
	int error;

	mutex_enter(&sc->lock);
	error = sc->core.fault;
	mutex_exit(&sc->lock);
	return error;
}
