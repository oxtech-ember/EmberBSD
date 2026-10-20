/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted native PV0 IRQ lifetime. No display BO retirement is inferred. */
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpi_intr.h>
#include <drm/drm_drv.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_mmu4_native.h"
#include "bcmv3d_vblank.h"
#include "bcmv3d_vblank_native.h"

_Static_assert(sizeof(struct bcmv3d_vblank_snapshot) == 80,
    "native PV0 snapshot v1 size");

struct bcmv3d_vblank_native {
	struct bcmv3d_device *sc;
	bus_space_tag_t bst;
	bus_space_handle_t pv, hvs;
	kmutex_t lock;
	struct bcmv3d_vblank core;
	void *ih;
	bcmv3d_vblank_notify_fn notify;
	void *arg;
	int fault;
	bool masked;
};

static void
vblank_native_copy(struct bcmv3d_vblank_native *n,
    struct bcmv3d_vblank_snapshot *s)
{

	KASSERT(mutex_owned(&n->lock));
	memset(s, 0, sizeof(*s));
	s->version = 1;
	s->length = sizeof(*s);
	s->sequence = n->core.sequence;
	s->spurious = n->core.spurious;
	s->prepared = n->core.prepared;
	s->arm_attempted = n->core.armed;
	s->routed = n->ih != NULL && !n->masked;
	s->first_error = n->fault;
	s->channel_status = n->core.channel_status;
	s->active_list = n->core.active_list;
	s->frame_count = n->core.frame_count;
	s->last_window = n->core.last_window;
	s->last_offset = n->core.last_offset;
	s->last_raw = n->core.last_raw;
	s->raw_valid = n->core.raw_valid;
	s->reject_mask = n->core.reject_mask;
	s->reject_expected = n->core.reject_expected;
}

static int
vblank_native_read(void *arg, enum bcmv3d_vblank_window w, uint32_t offset,
    uint32_t *out)
{
	struct bcmv3d_vblank_native *n = arg;
	bus_space_handle_t h;
	uint32_t length;

	KASSERT(mutex_owned(&n->lock));
	if (out == NULL || (w != BCMV3D_VBLANK_PV0 && w != BCMV3D_VBLANK_HVS))
		return EINVAL;
	h = w == BCMV3D_VBLANK_PV0 ? n->pv : n->hvs;
	length = w == BCMV3D_VBLANK_PV0 ? 0x100 : 0x1a000;
	if ((offset & 3U) != 0 || offset > length - 4)
		return EINVAL;
	/* ARM64 onfault recovery is process-only; never overwrite it in IRQ. */
	if (cpu_intr_p() || cpu_softintr_p()) {
		*out = bus_space_read_4(n->bst, h, offset);
		return 0;
	}
	return bus_space_peek_4(n->bst, h, offset, out) == 0 ? 0 : EIO;
}

static int
vblank_native_write(void *arg, enum bcmv3d_vblank_window w, uint32_t offset,
    uint32_t value)
{
	struct bcmv3d_vblank_native *n = arg;

	KASSERT(mutex_owned(&n->lock));
	if (w != BCMV3D_VBLANK_PV0 || (offset != 0x24 && offset != 0x28) ||
	    (offset == 0x28 ? value != 0x80 : value != 0 && value != 0x80))
		return EACCES;
	if (cpu_intr_p() || cpu_softintr_p()) {
		bus_space_write_4(n->bst, n->pv, offset, value);
		return 0;
	}
	return bus_space_poke_4(n->bst, n->pv, offset, value) == 0 ? 0 : EIO;
}

static void
vblank_native_barrier(void *arg, enum bcmv3d_vblank_window w)
{
	struct bcmv3d_vblank_native *n = arg;

	KASSERT(mutex_owned(&n->lock) && w == BCMV3D_VBLANK_PV0);
	bus_space_barrier(n->bst, n->pv, 0, 0x100,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
}

static void
vblank_native_fault(struct bcmv3d_vblank_native *n, int error)
{

	KASSERT(mutex_owned(&n->lock));
	if (n->fault == 0)
		n->fault = error;
	/* A failed display I/O permits no further display MMIO, even to mask. */
	if (n->ih != NULL && !n->masked) {
		acpi_intr_mask(n->ih);
		n->masked = true;
	}
}

static int
vblank_native_intr(void *arg)
{
	struct bcmv3d_vblank_native *n = arg;
	struct bcmv3d_vblank_snapshot snapshot;
	bcmv3d_vblank_notify_fn notify;
	void *notify_arg;
	bool handled;
	int error;

	mutex_enter(&n->lock);
	/* establish may dispatch a spurious IRQ before it returns its handle. */
	if (!n->core.armed || n->fault != 0) {
		mutex_exit(&n->lock);
		return 0;
	}
	error = bcmv3d_vblank_irq(&n->core, &handled);
	if (error != 0)
		vblank_native_fault(n, error);
	notify = n->notify;
	notify_arg = n->arg;
	if ((handled || error != 0) && notify != NULL)
		vblank_native_copy(n, &snapshot);
	mutex_exit(&n->lock);
	if ((handled || error != 0) && notify != NULL)
		notify(notify_arg, &snapshot);
	return error != 0 || handled;
}

int
bcmv3d_vblank_native_create(struct bcmv3d_device *sc,
    const struct bcm2712_display_contract *contract, bus_space_handle_t pv,
    bus_space_handle_t hvs, uint32_t variant, struct bcmv3d_vblank_native **out)
{
	static const struct bcmv3d_vblank_io io = {
		.read = vblank_native_read, .write = vblank_native_write,
		.barrier = vblank_native_barrier
	};
	struct bcmv3d_mmu4_native_status status;
	struct bcmv3d_vblank_native *n;
	struct acpi_irq irq;
	void *ih;
	int error;

	if (sc == NULL || contract == NULL || out == NULL || sc->drm == NULL ||
	    sc->bst == NULL || sc->mmu4_native == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (mutex_owned(&sc->memory_lock))
		return EDEADLK;
	if (contract->version != BCM2712_DISPLAY_VERSION ||
	    contract->irq[2] != 133 ||
	    contract->memory[3].address != UINT64_C(0x107c580000) ||
	    contract->memory[3].length != 0x1a000 ||
	    contract->memory[5].address != UINT64_C(0x107c410000) ||
	    contract->memory[5].length != 0x100)
		return ENXIO;
	error = bcmv3d_mmu4_native_cached_status(sc->mmu4_native, &status);
	if (error != 0)
		return error;
	if (status.stage != BCMV3D_MMU4_NATIVE_COMPLETE || status.fault != 0 ||
	    status.program_error != 0 || status.prepare_error != 0 ||
	    status.operation_error != 0 || status.close_error != 0 ||
	    status.status_error != 0 || !status.sealed || !status.backing_valid)
		return EBUSY;
	n = kmem_zalloc(sizeof(*n), KM_SLEEP);
	n->sc = sc;
	n->bst = sc->bst;
	n->pv = pv;
	n->hvs = hvs;
	mutex_init(&n->lock, MUTEX_DEFAULT, IPL_VM);
	mutex_enter(&sc->memory_lock);
	if (sc->vblank_native != NULL) {
		mutex_exit(&sc->memory_lock);
		mutex_destroy(&n->lock);
		kmem_free(n, sizeof(*n));
		return EBUSY;
	}
	drm_dev_get(sc->drm);
	sc->vblank_native = n;
	mutex_exit(&sc->memory_lock);
	/* Once published, this real owner and its resources survive every fault. */
	mutex_enter(&n->lock);
	error = bcmv3d_vblank_prepare(&n->core, &io, n, variant);
	if (error != 0)
		vblank_native_fault(n, error);
	mutex_exit(&n->lock);
	if (error != 0)
		return error;
	memset(&irq, 0, sizeof(irq));
	irq.ar_irq = contract->irq[2];
	irq.ar_type = ACPI_LEVEL_SENSITIVE;
	/* prepare masked PV0, verified INTEN0 and cleared VFP before routing. */
	ih = acpi_intr_establish_irq(sc->dev, &irq, IPL_VM, true,
	    vblank_native_intr, n, "bcmv3d-pv0");
	mutex_enter(&n->lock);
	n->ih = ih;
	error = ih == NULL ? ENXIO : bcmv3d_vblank_arm(&n->core);
	if (error != 0)
		vblank_native_fault(n, error);
	mutex_exit(&n->lock);
	if (error != 0)
		return error;
	*out = n;
	return 0;
}

int
bcmv3d_vblank_native_snapshot(struct bcmv3d_vblank_native *n,
    struct bcmv3d_vblank_snapshot *out)
{

	if (n == NULL || out == NULL)
		return EINVAL;
	mutex_enter(&n->lock);
	vblank_native_copy(n, out);
	mutex_exit(&n->lock);
	return 0;
}

int
bcmv3d_vblank_native_bind_snapshot(struct bcmv3d_vblank_native *n,
    bcmv3d_vblank_notify_fn notify, void *arg,
    struct bcmv3d_vblank_snapshot *initial)
{
	int error;

	if (n == NULL || notify == NULL || arg == NULL || initial == NULL)
		return EINVAL;
	mutex_enter(&n->lock);
	error = n->fault != 0 ? n->fault : !n->core.armed ? ENXIO :
	    n->notify != NULL ? EBUSY : 0;
	if (error == 0) {
		vblank_native_copy(n, initial);
		n->arg = arg;
		n->notify = notify;
	}
	mutex_exit(&n->lock);
	return error;
}

int
bcmv3d_vblank_native_bind(struct bcmv3d_vblank_native *n,
    bcmv3d_vblank_notify_fn notify, void *arg)
{
	struct bcmv3d_vblank_snapshot initial;

	return bcmv3d_vblank_native_bind_snapshot(n, notify, arg, &initial);
}
