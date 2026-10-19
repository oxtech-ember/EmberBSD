/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted fixed native MMU4 consumer. No default caller or release path. */
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <drm/drm_drv.h>
#include <arm/broadcom/bcm2712_display_acpi.h>
#include <arm/broadcom/bcm2712_iommuc.h>
#include <arm/broadcom/bcm2712_iommuc_executor.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_mmu4_native.h"

#define MMU4_RESOURCE	4
#define MMU4_ADDRESS	UINT64_C(0x1000005200)
#define MMU4_LENGTH	0x80U
#define MMU4_MEMBER	0x2U

struct bcmv3d_mmu4_native {
	struct bcmv3d_device *sc;
	struct drm_device *drm;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	struct bcm2712_iommuc_provider *provider;
	kmutex_t lock;
	struct bcmv3d_mmu4_backing *backing;
	struct bcm2712_mmu4_config cfg;
	struct bcmv3d_mmu4_native_status status;
	/* Borrowed only during one synchronous executor callback. */
	struct bcm2712_iommuc_core *cache;
	struct bcm2712_mmu4_core *active;
	bool executing, barrier_pending;
};

static int
mmu4_native_context(void)
{

	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	return 0;
}

static int
mmu4_native_enter(struct bcmv3d_mmu4_native *n)
{
	int error;

	if (n == NULL)
		return EINVAL;
	if ((error = mmu4_native_context()) != 0)
		return error;
	if (n->sc == NULL || n->sc->mmu4_native != n ||
	    n->drm == NULL || n->sc->drm != n->drm || n->bst == NULL ||
	    n->sc->bst != n->bst)
		return ENXIO;
	if (mutex_owned(&n->lock))
		return EDEADLK;
	mutex_enter(&n->lock);
	return 0;
}

static int
mmu4_native_fault(struct bcmv3d_mmu4_native *n, int error)
{

	if (n->status.fault == 0)
		n->status.fault = error;
	return n->status.fault;
}

static int
mmu4_native_prefix(struct bcmv3d_mmu4_native *n, bool observe)
{
	struct bcm2712_iommuc_native_status status;
	const struct bcm2712_iommuc_observation *o;
	int error, snapshot_error;

	n->status.icac_valid = false;
	error = bcm2712_iommuc_cached_status(n->provider, &status);
	if (error != 0)
		return error;
	n->status.icac = status;
	n->status.icac_valid = true;
	if (!status.software_attached)
		return ENXIO;
	/* A consumed provider result precedes state changed by that attempt. */
	if (status.mmu4.attempted)
		return status.mmu4.first_error != 0 ?
		    status.mmu4.first_error : EALREADY;
	if (status.mmu4.first_error != 0 || status.mmu4.fault != 0)
		return EIO;
	if (status.core_initialized)
		return EBUSY;
	o = &status.observation;
	if (!o->started) {
		if (!observe || o->stage != BCM2712_IOMMUC_OBSERVE_NONE ||
		    o->mapped || o->raw_valid || o->reads_attempted != 0 ||
		    o->reads_completed != 0 || o->first_error != 0 ||
		    o->raw_control != 0)
			return EBUSY;
		error = bcm2712_iommuc_observe_control(n->provider);
		n->status.icac_valid = false;
		snapshot_error = bcm2712_iommuc_cached_status(n->provider,
		    &status);
		if (snapshot_error == 0) {
			n->status.icac = status;
			n->status.icac_valid = true;
		}
		n->status.status_error = snapshot_error;
		/* EALREADY is not converted to success by an accepted snapshot. */
		if (error != 0)
			return error;
		if (snapshot_error != 0)
			return snapshot_error;
	}
	o = &status.observation;
	if (o->first_error != 0)
		return o->first_error;
	if (!status.software_attached || status.core_initialized ||
	    status.mmu4.attempted || status.mmu4.first_error != 0 ||
	    status.mmu4.fault != 0 || !o->started || !o->mapped ||
	    !o->raw_valid || o->stage != BCM2712_IOMMUC_OBSERVE_COMPLETE ||
	    o->reads_attempted != 1 || o->reads_completed != 1)
		return EBUSY;
	if ((o->raw_control & ~0x7U) != 0)
		return EOPNOTSUPP;
	if ((o->raw_control & 0x4U) != 0)
		return EBUSY;
	return 0;
}

int
bcmv3d_mmu4_native_create(struct bcmv3d_device *sc,
    struct bcmv3d_mmu4_native **out)
{
	struct bcm2712_display_contract display;
	struct bcm2712_iommuc_contract contract;
	struct bcm2712_iommuc_provider *provider;
	struct bcmv3d_mmu4_native *n;
	struct acpi_devnode *node;
	ACPI_HANDLE gpu, cache;
	ACPI_OBJECT_TYPE type;
	cfattach_t attachment;
	int error;

	if (sc == NULL || out == NULL)
		return EINVAL;
	if ((error = mmu4_native_context()) != 0)
		return error;
	if (sc->dev == NULL || sc->drm == NULL || sc->bst == NULL ||
	    sc->dmat == NULL || sc->handle == NULL ||
	    !device_is_a(sc->dev, "bcmv3d") ||
	    sc->drm->dev_private != sc || sc->drm->dev != sc->dev ||
	    !sc->drm->registered || sc->drm->driver == NULL ||
	    sc->drm->driver->name == NULL ||
	    strcmp(sc->drm->driver->name, "v3d") != 0 ||
	    sc->drm->render == NULL || sc->drm->render->dev != sc->drm ||
	    sc->drm->render->type != DRM_MINOR_RENDER)
		return ENXIO;
	attachment = device_cfattach(sc->dev);
	if (attachment == NULL || attachment->ca_name == NULL ||
	    strcmp(attachment->ca_name, "bcmv3d_acpi") != 0)
		return ENXIO;
	if (mutex_owned(&sc->memory_lock))
		return EDEADLK;
	mutex_enter(&sc->memory_lock);
	error = sc->mmu4_native == NULL ? 0 : EBUSY;
	mutex_exit(&sc->memory_lock);
	if (error != 0)
		return error;
	if (ACPI_FAILURE(AcpiGetHandle(NULL, "\\_SB.GPU0", &gpu)) ||
	    gpu != sc->handle || ACPI_FAILURE(AcpiGetType(gpu, &type)) ||
	    type != ACPI_TYPE_DEVICE)
		return ENXIO;
	node = acpi_match_node(gpu);
	if (node == NULL || node->ad_type != ACPI_TYPE_DEVICE ||
	    node->ad_handle != gpu || node->ad_device != sc->dev)
		return ENXIO;
	error = bcm2712_display_acpi_resources(sc->dev, gpu, &display);
	if (error != 0)
		return error;
	if (display.version != BCM2712_DISPLAY_VERSION ||
	    display.memory[MMU4_RESOURCE].address != MMU4_ADDRESS ||
	    display.memory[MMU4_RESOURCE].length != MMU4_LENGTH)
		return EINVAL;
	error = bcm2712_iommuc_acpi_dependency(gpu, &cache, &contract);
	if (error != 0)
		return error;
	error = bcm2712_iommuc_lookup(cache, &provider);
	if (error != 0)
		return error;
	drm_dev_get(sc->drm);
	n = kmem_zalloc(sizeof(*n), KM_SLEEP);
	n->sc = sc;
	n->drm = sc->drm;
	n->bst = sc->bst;
	n->provider = provider;
	mutex_init(&n->lock, MUTEX_DEFAULT, IPL_NONE);
	/* Only this short publication holds the renderer's accounting lock. */
	mutex_enter(&sc->memory_lock);
	if (sc->mmu4_native != NULL) {
		mutex_exit(&sc->memory_lock);
		mutex_destroy(&n->lock);
		kmem_free(n, sizeof(*n));
		drm_dev_put(sc->drm);
		return EBUSY;
	}
	sc->mmu4_native = n;
	mutex_exit(&sc->memory_lock);
	*out = n;
	return 0;
}

int
bcmv3d_mmu4_native_prepare(struct bcmv3d_mmu4_native *n)
{
	int error;

	if ((error = mmu4_native_enter(n)) != 0)
		return error;
	if (n->status.prepare_attempted) {
		error = n->status.prepare_error != 0 ?
		    n->status.prepare_error : EALREADY;
		goto done;
	}
	if (n->status.fault != 0) {
		error = n->status.fault;
		goto done;
	}
	n->status.prepare_attempted = true;
	n->status.stage = BCMV3D_MMU4_NATIVE_PREFIX;
	error = mmu4_native_prefix(n, true);
	if (error != 0)
		goto result;
	n->status.stage = BCMV3D_MMU4_NATIVE_MAP;
	error = bus_space_map(n->bst, MMU4_ADDRESS, MMU4_LENGTH, 0, &n->bsh);
	if (error != 0)
		goto result;
	n->status.mapped = true;
	n->status.stage = BCMV3D_MMU4_NATIVE_DEBUG;
	aprint_normal_dev(n->sc->dev, "MMU4 one-shot DEBUG read: experimental "
	    "normal-active SoC assumption; clocks and firmware/MMU2/4/5/cache "
	    "exclusion required; bus stall/abort is not recoverable\n");
	n->status.reads_attempted++;
	n->status.debug = bus_space_read_4(n->bst, n->bsh, 0x38);
	n->status.reads_completed++;
	n->status.debug_valid = true;
	n->status.stage = BCMV3D_MMU4_NATIVE_ALLOCATE;
	error = bcmv3d_mmu4_backing_create(n->sc, n->status.debug, &n->backing);
	if (error != 0)
		goto result;
	n->status.backing_created = true;
	error = bcmv3d_mmu4_backing_get_config(n->backing, &n->cfg);
	if (error != 0)
		goto result;
	error = bcmv3d_mmu4_backing_status(n->backing, &n->status.backing);
	if (error == 0) {
		n->status.backing_valid = true;
		n->status.stage = BCMV3D_MMU4_NATIVE_PREPARED;
	}
result:
	n->status.prepare_error = error;
done:
	mutex_exit(&n->lock);
	return error;
}

/* Closed independent register facts from the pinned BCM2712 IOMMU reference. */
static int
mmu4_native_offset(enum bcm2712_mmu4_register reg, bool write,
    bus_size_t *offset)
{
	bus_size_t value;

	switch (reg) {
	case BCM2712_MMU4_DEBUG:
		if (write)
			return EINVAL;
		value = 0x38;
		break;
	case BCM2712_MMU4_CONTROL:
		value = 0;
		break;
	case BCM2712_MMU4_SHOOT_DOWN:
		if (write)
			return EINVAL;
		value = 0x18;
		break;
	case BCM2712_MMU4_MISC:
		value = 0x24;
		break;
	case BCM2712_MMU4_CAP:
	case BCM2712_MMU4_BYPASS_START:
	case BCM2712_MMU4_BYPASS_END:
	case BCM2712_MMU4_PT_BASE:
	case BCM2712_MMU4_ILLEGAL_ADR:
		if (!write)
			return EINVAL;
		value = reg == BCM2712_MMU4_CAP ? 0x14 :
		    reg == BCM2712_MMU4_BYPASS_START ? 0x1c :
		    reg == BCM2712_MMU4_BYPASS_END ? 0x20 :
		    reg == BCM2712_MMU4_PT_BASE ? 0x04 : 0x30;
		break;
	default:
		return EINVAL;
	}
	if ((value & 3U) != 0 || value > MMU4_LENGTH - 4)
		return EINVAL;
	*offset = value;
	return 0;
}

static int
mmu4_native_backend(struct bcmv3d_mmu4_native *n)
{

	if (!mutex_owned(&n->lock))
		return EDEADLK;
	if (!n->executing)
		return mmu4_native_fault(n, EPROTO);
	if (n->status.fault != 0)
		return n->status.fault;
	if (!n->status.mapped || !n->status.sealed || n->cache == NULL ||
	    n->active == NULL || !n->active->initialized)
		return mmu4_native_fault(n, EPROTO);
	if (n->cache->status.fault != 0)
		return mmu4_native_fault(n, n->cache->status.fault);
	if (n->active->status.fault != 0)
		return mmu4_native_fault(n, n->active->status.fault);
	return 0;
}

static int
mmu4_native_read(void *arg, enum bcm2712_mmu4_register reg, uint32_t *out)
{
	struct bcmv3d_mmu4_native *n = arg;
	bus_size_t offset;
	int error;

	if ((error = mmu4_native_backend(n)) != 0)
		return error;
	error = mmu4_native_offset(reg, false, &offset);
	if (error != 0 || out == NULL || n->barrier_pending)
		return mmu4_native_fault(n, error != 0 ? error : EPROTO);
	n->status.reads_attempted++;
	*out = bus_space_read_4(n->bst, n->bsh, offset);
	n->status.reads_completed++;
	return 0;
}

static int
mmu4_native_write(void *arg, enum bcm2712_mmu4_register reg, uint32_t value)
{
	struct bcmv3d_mmu4_native *n = arg;
	bus_size_t offset;
	int error;

	if ((error = mmu4_native_backend(n)) != 0)
		return error;
	/* Retain native storage BEFORE a first attempted local register write. */
	n->status.write_attempted = true;
	n->status.writes_attempted++;
	error = mmu4_native_offset(reg, true, &offset);
	if (error != 0 || n->barrier_pending || !n->active->status.retained ||
	    !n->active->status.mutation_attempted ||
	    (n->cache->status.reserved & MMU4_MEMBER) == 0 ||
	    (n->cache->status.retained & MMU4_MEMBER) == 0)
		return mmu4_native_fault(n, error != 0 ? error : EPROTO);
	n->barrier_pending = true;
	bus_space_write_4(n->bst, n->bsh, offset, value);
	n->status.writes_completed++;
	return 0;
}

static void
mmu4_native_barrier(void *arg)
{
	struct bcmv3d_mmu4_native *n = arg;

	if (mmu4_native_backend(n) != 0)
		return;
	if (!n->barrier_pending) {
		(void)mmu4_native_fault(n, EPROTO);
		return;
	}
	bus_space_barrier(n->bst, n->bsh, 0, MMU4_LENGTH,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	n->status.barriers++;
	n->barrier_pending = false;
}

static int
mmu4_native_pages(void *arg, const struct bcm2712_mmu4_config *cfg)
{
	struct bcmv3d_mmu4_native *n = arg;
	int error;

	if ((error = mmu4_native_backend(n)) != 0)
		return error;
	return bcmv3d_mmu4_backing_prepare(n->backing, cfg);
}

static int
mmu4_native_operation(void *arg, struct bcm2712_iommuc_core *cache)
{
	struct bcmv3d_mmu4_native *n = arg;
	struct bcm2712_mmu4_core sequence;
	const struct bcm2712_mmu4_io io = {
		.prepare_pages = mmu4_native_pages,
		.read = mmu4_native_read,
		.write = mmu4_native_write,
		.barrier = mmu4_native_barrier,
		.arg = n
	};
	int error, status_error, close_error = 0;

	if (!mutex_owned(&n->lock) || n->executing || cache == NULL ||
	    !n->status.sealed)
		return EPROTO;
	memset(&sequence, 0, sizeof(sequence));
	n->cache = cache;
	n->active = &sequence;
	n->executing = true;
	error = bcm2712_mmu4_core_init(&sequence, &n->cfg, &io, cache);
	if (error != 0)
		goto done;
	error = bcm2712_mmu4_core_program(&sequence);
	status_error = bcm2712_mmu4_core_status(&sequence, &n->status.sequence);
	n->status.sequence_valid = status_error == 0;
	if (status_error != 0) {
		n->status.status_error = status_error;
		if (error == 0)
			error = status_error;
		(void)mmu4_native_fault(n, status_error);
	} else if (error != 0 && !n->status.sequence.retained &&
	    !n->status.sequence.mutation_attempted &&
	    n->status.sequence.fault == 0 && n->status.fault == 0) {
		/* Keep the value snapshot above; fini may clear its own lease. */
		close_error = bcm2712_mmu4_core_fini(&sequence);
	}
	n->status.close_error = close_error;
	if (close_error != 0)
		(void)mmu4_native_fault(n, close_error);
	if (n->status.sequence_valid && n->status.sequence.fault != 0)
		(void)mmu4_native_fault(n, n->status.sequence.fault);
	status_error = bcm2712_iommuc_core_status(cache, &n->status.shared);
	n->status.shared_valid = status_error == 0;
	if (status_error != 0) {
		n->status.status_error = status_error;
		(void)mmu4_native_fault(n, status_error);
		if (error == 0)
			error = status_error;
	}
done:
	/* No stack/core/cache address escapes this synchronous operation. */
	n->executing = false;
	n->active = NULL;
	n->cache = NULL;
	n->status.operation_error = error;
	return error != 0 ? error : close_error;
}

int
bcmv3d_mmu4_native_program(struct bcmv3d_mmu4_native *n)
{
	int error, status_error;

	if ((error = mmu4_native_enter(n)) != 0)
		return error;
	if (n->status.program_attempted) {
		error = n->status.program_error != 0 ?
		    n->status.program_error : EALREADY;
		goto done;
	}
	if (n->status.fault != 0) {
		error = n->status.fault;
		goto done;
	}
	if (!n->status.prepare_attempted || n->status.prepare_error != 0 ||
	    n->status.stage != BCMV3D_MMU4_NATIVE_PREPARED ||
	    !n->status.backing_created || !n->status.backing_valid) {
		error = EBUSY;
		goto done;
	}
	n->status.program_attempted = true;
	n->status.stage = BCMV3D_MMU4_NATIVE_PROGRAM;
	error = mmu4_native_prefix(n, false);
	if (error != 0)
		goto result;
	error = bcmv3d_mmu4_backing_seal(n->backing);
	if (error != 0)
		goto result;
	n->status.sealed = true;
	n->status.backing_valid = false;
	error = bcmv3d_mmu4_backing_status(n->backing, &n->status.backing);
	if (error != 0)
		goto result;
	n->status.backing_valid = true;
	if (!n->status.backing.sealed) {
		error = EPROTO;
		goto result;
	}
	error = bcm2712_iommuc_execute_mmu4(n->provider,
	    mmu4_native_operation, n);
	n->status.icac_valid = false;
	status_error = bcm2712_iommuc_cached_status(n->provider, &n->status.icac);
	if (status_error == 0)
		n->status.icac_valid = true;
	else {
		n->status.status_error = status_error;
		(void)mmu4_native_fault(n, status_error);
		if (error == 0)
			error = status_error;
	}
	if (n->status.icac_valid && n->status.icac.mmu4.fault != 0)
		(void)mmu4_native_fault(n, n->status.icac.mmu4.fault);
	/* The executor can prepare backing even when later programming fails. */
	n->status.backing_valid = false;
	status_error = bcmv3d_mmu4_backing_status(n->backing,
	    &n->status.backing);
	if (status_error == 0)
		n->status.backing_valid = true;
	else {
		if (n->status.status_error == 0)
			n->status.status_error = status_error;
		(void)mmu4_native_fault(n, status_error);
		if (error == 0)
			error = status_error;
	}
	if (error == 0 && n->status.fault != 0)
		error = n->status.fault;
	if (error == 0)
		n->status.stage = BCMV3D_MMU4_NATIVE_COMPLETE;
result:
	n->status.program_error = error;
done:
	mutex_exit(&n->lock);
	return error;
}

int
bcmv3d_mmu4_native_cached_status(struct bcmv3d_mmu4_native *n,
    struct bcmv3d_mmu4_native_status *out)
{
	int error;

	if (out == NULL)
		return EINVAL;
	if ((error = mmu4_native_enter(n)) != 0)
		return error;
	*out = n->status;
	mutex_exit(&n->lock);
	return 0;
}
