/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted retained HVS consumer. No firmware framebuffer copying. */
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/systm.h>
#include <sys/workqueue.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <arm/broadcom/bcm2712_display_hvsvar.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_flip.h"
#include "bcmv3d_read.h"
#include "bcmv3d_mmu4_native.h"
#include "bcmv3d_vblank_native.h"
#include "bcmv3d_scanout.h"

struct bcmv3d_scanout_frame {
	struct bcmv3d_scanout *owner;
	struct drm_gem_object *object;
	struct bcmv3d_read read;
	struct bcm2712_hvs_dlist list;
	unsigned int slot, refs;
	bool preparing, staged, published, exposed, completion_committed;
};

struct bcmv3d_scanout {
	struct bcmv3d_device *sc;
	bus_space_tag_t bst;
	bus_space_handle_t hvs;
	kmutex_t process_lock, lock;
	struct workqueue *workqueue;
	struct work work;
	struct bcmv3d_flip flip;
	struct bcmv3d_scanout_frame frame[BCMV3D_FLIP_GEM_SLOTS];
	bcmv3d_scanout_notify_fn notify;
	void *notify_arg;
	uint64_t latches, prepared, retired;
	uint32_t variant, lptr, control1;
	bool work_pending;
};

static void
scanout_schedule(struct bcmv3d_scanout *n)
{

	KASSERT(mutex_owned(&n->lock));
	if (!n->work_pending) {
		n->work_pending = true;
		workqueue_enqueue(n->workqueue, &n->work, NULL);
	}
}

static int
scanout_fault(struct bcmv3d_scanout *n, int error)
{

	KASSERT(mutex_owned(&n->lock));
	error = bcmv3d_flip_fail(&n->flip, error > 0 ? error : EIO);
	scanout_schedule(n);
	return error;
}

static int
scanout_enter(struct bcmv3d_scanout *n)
{

	if (n == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (mutex_owned(&n->sc->memory_lock))
		return EDEADLK;
	mutex_enter(&n->process_lock);
	return 0;
}

static void
scanout_drop_software(struct bcmv3d_scanout_frame *f)
{

	KASSERT(mutex_owned(&f->owner->process_lock));
	KASSERT(f->refs == 0 && !f->preparing && !f->staged &&
	    !f->published && !f->exposed && f->read.fence == NULL);
	if (f->object != NULL)
		drm_gem_object_put_unlocked(f->object);
	f->object = NULL;
}

/* Never runs under the IRQ mutex or the BO reservation lock. */
static void
scanout_work(struct work *work, void *arg)
{
	struct bcmv3d_scanout *n = arg;
	struct bcmv3d_scanout_frame *f;
	unsigned int i;
	int error, fault;

	KASSERT(work == &n->work);
	mutex_enter(&n->process_lock);
	for (;;) {
		mutex_enter(&n->lock);
		fault = n->flip.fault;
		for (i = 0; fault == 0 && i < BCMV3D_FLIP_SLOTS; i++) {
			if (n->flip.slot[i].state == BCMV3D_FLIP_RETIRED)
				break;
		}
		if (fault != 0 || i == BCMV3D_FLIP_SLOTS) {
			n->work_pending = false;
			mutex_exit(&n->lock);
			if (fault != 0) {
				for (i = 0; i < BCMV3D_FLIP_GEM_SLOTS; i++) {
					f = &n->frame[i];
					/* prepare owns read storage outside our mutex. */
					if (!f->preparing && !f->completion_committed &&
					    f->read.fence != NULL)
						(void)bcmv3d_read_poison(&f->read,
						    fault);
				}
			}
			break;
		}
		error = bcmv3d_flip_claim_retired(&n->flip, i);
		if (error != 0)
			(void)scanout_fault(n, error);
		mutex_exit(&n->lock);
		if (error != 0)
			continue;
		if (i == BCMV3D_FLIP_BLANK) {
			mutex_enter(&n->lock);
			error = bcmv3d_flip_reclaimed(&n->flip, i);
			if (error != 0)
				(void)scanout_fault(n, error);
			mutex_exit(&n->lock);
			continue;
		}
		f = &n->frame[i];
		error = bcmv3d_mmu4_native_retire_gem(n->sc->mmu4_native, i, f);
		mutex_enter(&n->lock);
		if (error == 0 && n->flip.fault != 0)
			error = n->flip.fault;
		if (error == 0 && (f != &n->frame[i] || f->object == NULL ||
		    n->flip.slot[i].state != BCMV3D_FLIP_RECLAIMING))
			error = EPROTO;
		/* Actual POST/unload is complete. A later fault cannot revoke it. */
		if (error == 0)
			f->completion_committed = true;
		if (error != 0)
			(void)scanout_fault(n, error);
		mutex_exit(&n->lock);
		if (error != 0)
			continue;
		/* Map POSTWRITE/unload/ref release precedes signalling its reader. */
		error = bcmv3d_read_end(&f->read);
		mutex_enter(&n->lock);
		if (error == 0)
			error = bcmv3d_flip_reclaimed(&n->flip, i);
		if (error != 0)
			(void)scanout_fault(n, error);
		else
			n->retired++;
		mutex_exit(&n->lock);
		if (error != 0)
			continue;
		f->staged = f->published = f->exposed = false;
		if (f->refs == 0)
			scanout_drop_software(f);
	}
	mutex_exit(&n->process_lock);
}

static void
scanout_vblank(void *arg, const struct bcmv3d_vblank_snapshot *snapshot)
{
	struct bcmv3d_scanout *n = arg;
	struct bcmv3d_flip_event event = {
		.sequence = snapshot->sequence,
		.active_list = snapshot->active_list,
		.frame_count = snapshot->frame_count,
		.first_error = snapshot->first_error
	};
	struct bcmv3d_flip_update update = { 0 };
	bcmv3d_scanout_notify_fn notify;
	void *notify_arg;
	uint64_t prior;
	int error;

	mutex_enter(&n->lock);
	prior = n->flip.sequence;
	error = bcmv3d_flip_observe(&n->flip, &event, &update);
	if (error != 0)
		error = scanout_fault(n, error);
	if (update.latched)
		n->latches++;
	if (update.retired_mask != 0)
		scanout_schedule(n);
	notify = n->notify;
	notify_arg = n->notify_arg;
	mutex_exit(&n->lock);
	if (notify != NULL && (error != 0 || snapshot->sequence > prior))
		notify(notify_arg, snapshot->sequence, update.latched, error);
}

int
bcmv3d_scanout_create(struct bcmv3d_device *sc,
    const struct bcm2712_display_contract *contract, bus_space_handle_t hvs,
    uint32_t variant, uint32_t cxm_words, struct bcmv3d_scanout **out)
{
	struct bcmv3d_vblank_snapshot snapshot;
	struct bcmv3d_flip_event event, initial;
	struct bcmv3d_scanout *n;
	unsigned int i;
	int error;

	if (sc == NULL || contract == NULL || out == NULL || sc->drm == NULL ||
	    sc->bst == NULL || sc->mmu4_native == NULL || sc->vblank_native == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (mutex_owned(&sc->memory_lock))
		return EDEADLK;
	if (contract->version != BCM2712_DISPLAY_VERSION ||
	    contract->memory[3].address != UINT64_C(0x107c580000) ||
	    contract->memory[3].length != 0x1a000 || cxm_words < 73 ||
	    (variant != 0x53 && variant != 0x54))
		return ENXIO;
	error = bcmv3d_vblank_native_snapshot(sc->vblank_native, &snapshot);
	if (error != 0)
		return error;
	if (snapshot.first_error != 0 || snapshot.frame_count >= 64 ||
	    !snapshot.prepared ||
	    !snapshot.arm_attempted || !snapshot.routed || snapshot.active_list != 32)
		return EBUSY;
	n = kmem_zalloc(sizeof(*n), KM_SLEEP);
	n->sc = sc;
	n->bst = sc->bst;
	n->hvs = hvs;
	n->variant = variant;
	n->lptr = variant == 0x53 ? 0x3c : 0x110;
	n->control1 = variant == 0x53 ? 0x34 : 0x104;
	mutex_init(&n->process_lock, MUTEX_DEFAULT, IPL_NONE);
	mutex_init(&n->lock, MUTEX_DEFAULT, IPL_VM);
	event = (struct bcmv3d_flip_event) { .sequence = snapshot.sequence,
	    .active_list = snapshot.active_list, .frame_count = snapshot.frame_count };
	initial = event;
	for (i = 0; i < BCMV3D_FLIP_GEM_SLOTS; i++) {
		n->frame[i].owner = n;
		n->frame[i].slot = i;
	}
	error = workqueue_create(&n->workqueue, "v3dscan", scanout_work, n,
	    PRI_NONE, IPL_VM, WQ_MPSAFE);
	if (error != 0)
		goto free;
	mutex_enter(&sc->memory_lock);
	if (sc->scanout != NULL) {
		mutex_exit(&sc->memory_lock);
		error = EBUSY;
		workqueue_destroy(n->workqueue);
		goto free;
	}
	drm_dev_get(sc->drm);
	sc->scanout = n;
	mutex_exit(&sc->memory_lock);
	/* Real registry publication is irreversible, including a bind failure. */
	mutex_enter(&n->lock);
	error = bcmv3d_vblank_native_bind_snapshot(sc->vblank_native,
	    scanout_vblank, n, &snapshot);
	event = error != 0 ? initial : (struct bcmv3d_flip_event) {
	    .sequence = snapshot.sequence, .active_list = snapshot.active_list,
	    .frame_count = snapshot.frame_count, .first_error = snapshot.first_error };
	if (bcmv3d_flip_init(&n->flip, &event) != 0) {
		/* The failed initialization published no state. Do not reset it. */
		error = bcmv3d_flip_init(&n->flip, &initial);
		KASSERT(error == 0);
		if (error == 0)
			error = EPROTO;
	}
	if (error != 0)
		(void)scanout_fault(n, error);
	mutex_exit(&n->lock);
	if (error != 0)
		return error;
	*out = n;
	return 0;
free:
	mutex_destroy(&n->lock);
	mutex_destroy(&n->process_lock);
	kmem_free(n, sizeof(*n));
	return error;
}

int
bcmv3d_scanout_bind(struct bcmv3d_scanout *n,
    bcmv3d_scanout_notify_fn notify, void *arg)
{
	int error;

	if (n == NULL || notify == NULL || arg == NULL)
		return EINVAL;
	mutex_enter(&n->lock);
	error = n->flip.fault != 0 ? n->flip.fault :
	    n->notify != NULL ? EBUSY : 0;
	if (error == 0) {
		n->notify_arg = arg;
		n->notify = notify;
	}
	mutex_exit(&n->lock);
	return error;
}

/* The process owner serializes cleanup. No exposed consumer may enter here. */
static int
scanout_cancel_unexposed(struct bcmv3d_scanout_frame *f)
{
	struct bcmv3d_scanout *n = f->owner;
	int error;

	KASSERT(mutex_owned(&n->process_lock));
	KASSERT(!f->preparing && !f->exposed);
	mutex_enter(&n->lock);
	error = n->flip.fault;
	mutex_exit(&n->lock);
	if (error != 0)
		return error;
	if (f->published)
		error = bcmv3d_mmu4_native_unmap_gem(n->sc->mmu4_native, f->slot);
	else if (f->staged)
		error = bcmv3d_mmu4_native_discard_staged_gem(n->sc->mmu4_native,
		    f->slot);
	if (error == 0)
		f->staged = f->published = false;
	mutex_enter(&n->lock);
	if (error == 0 && n->flip.fault != 0)
		error = n->flip.fault;
	/* The never-exposed map is gone. A later fault cannot revoke cleanup. */
	if (error == 0)
		f->completion_committed = true;
	else
		(void)scanout_fault(n, error);
	mutex_exit(&n->lock);
	if (error != 0)
		return error;
	error = bcmv3d_read_end(&f->read);
	mutex_enter(&n->lock);
	if (error == 0)
		error = bcmv3d_flip_cancel(&n->flip, f->slot);
	if (error != 0)
		(void)scanout_fault(n, error);
	mutex_exit(&n->lock);
	return error;
}

int
bcmv3d_scanout_reserve(struct bcmv3d_scanout *n, struct drm_gem_object *obj,
    struct bcmv3d_scanout_frame **out)
{
	struct bcm2712_hvs_frame plane;
	struct bcmv3d_scanout_frame *f = NULL;
	struct bcmv3d_bo *bo;
	enum bcmv3d_flip_state state;
	unsigned int i;
	int error, cleanup;

	if (n == NULL || obj == NULL || out == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	if (mutex_owned(&n->sc->memory_lock))
		return EDEADLK;
	/* Validate native ownership and retain software state independently. */
	error = bcmv3d_bo_reference(n->sc, obj, &bo);
	if (error != 0)
		return error;
	if ((error = scanout_enter(n)) != 0)
		goto put;
	mutex_enter(&n->lock);
	error = n->flip.fault;
	for (i = 0; error == 0 && i < BCMV3D_FLIP_GEM_SLOTS; i++) {
		f = &n->frame[i];
		state = n->flip.slot[i].state;
		if (f->object != obj)
			continue;
		/* A writer can hold resv while waiting for our existing reader. */
		if (f->preparing || (state != BCMV3D_FLIP_RESERVED &&
		    state != BCMV3D_FLIP_READY && state != BCMV3D_FLIP_PENDING &&
		    state != BCMV3D_FLIP_CURRENT))
			error = EBUSY;
		else if (f->refs == UINT_MAX)
			error = EOVERFLOW;
		else
			f->refs++;
		mutex_exit(&n->lock);
		if (error == 0)
			*out = f;
		mutex_exit(&n->process_lock);
		goto put;
	}
	f = NULL;
	for (i = 0; error == 0 && i < BCMV3D_FLIP_GEM_SLOTS; i++) {
		if (n->frame[i].object == NULL && n->frame[i].refs == 0 &&
		    n->flip.slot[i].state == BCMV3D_FLIP_FREE) {
			f = &n->frame[i];
			break;
		}
	}
	if (error == 0 && f == NULL)
		error = EBUSY;
	if (error == 0)
		error = bcmv3d_flip_reserve(&n->flip, f->slot);
	if (error == 0) {
		f->object = obj;
		f->refs = 1;
		f->preparing = true;
		f->completion_committed = false;
	}
	mutex_exit(&n->lock);
	mutex_exit(&n->process_lock);
	if (error != 0)
		goto put;
	/* Reservation locking never nests inside a native owner lock. */
	error = bcmv3d_read_reserve(&f->read, obj);
	mutex_enter(&n->process_lock);
	f->preparing = false;
	mutex_enter(&n->lock);
	if (error == 0)
		error = n->flip.fault;
	mutex_exit(&n->lock);
	if (error != 0)
		goto cancel;
	error = bcmv3d_mmu4_native_stage_gem(n->sc->mmu4_native, f->slot, obj);
	if (error != 0)
		goto cancel;
	f->staged = true;
	plane = (struct bcm2712_hvs_frame) { .variant = n->variant,
	    .dma_base = (uint64_t)(f->slot + 1) * 8 * 1024 * 1024,
	    .dma_span = 8294400, .upm_offset = (f->slot + 1) * 15616,
	    .upm_length = 15616, .upm_slot = f->slot + 1 };
	error = bcm2712_hvs_encode_1080p60(&plane, &f->list);
	if (error != 0)
		goto cancel;
	/* PTE publication is not HVS exposure and does not sync producer data. */
	f->published = true;
	error = bcmv3d_mmu4_native_publish_staged_gem(n->sc->mmu4_native, f->slot);
	if (error != 0)
		goto retain;
	mutex_enter(&n->lock);
	error = n->flip.fault;
	mutex_exit(&n->lock);
	if (error != 0)
		goto retain;
	*out = f;
	mutex_exit(&n->process_lock);
	return 0;
cancel:
	cleanup = scanout_cancel_unexposed(f);
	if (cleanup != 0) {
		error = cleanup;
		goto retain;
	}
	f->refs = 0;
	scanout_drop_software(f);
	mutex_exit(&n->process_lock);
	return error;
retain:
	/* No software reference escapes, but uncertain resources survive. */
	f->refs = 0;
	mutex_enter(&n->lock);
	(void)scanout_fault(n, error);
	mutex_exit(&n->lock);
	mutex_exit(&n->process_lock);
	return error;
put:
	drm_gem_object_put_unlocked(&bo->cma.base);
	return error;
}

int
bcmv3d_scanout_finalize(struct bcmv3d_scanout_frame *f, long timeout)
{
	struct bcmv3d_scanout *n;
	enum bcmv3d_flip_state state;
	int error;

	if (f == NULL || f->owner == NULL || timeout <= 0)
		return EINVAL;
	n = f->owner;
	if (f->slot >= BCMV3D_FLIP_GEM_SLOTS || f != &n->frame[f->slot])
		return EPERM;
	if ((error = scanout_enter(n)) != 0)
		return error;
	mutex_enter(&n->lock);
	state = n->flip.slot[f->slot].state;
	error = n->flip.fault != 0 ? n->flip.fault :
	    f->refs == 0 || f->object == NULL ? EINVAL :
	    f->preparing ? EBUSY :
	    state == BCMV3D_FLIP_RESERVED || state == BCMV3D_FLIP_READY ||
	    state == BCMV3D_FLIP_PENDING || state == BCMV3D_FLIP_CURRENT ? 0 : EBUSY;
	if (error == 0 && state == BCMV3D_FLIP_RESERVED)
		f->preparing = true;
	mutex_exit(&n->lock);
	mutex_exit(&n->process_lock);
	if (error != 0 || state != BCMV3D_FLIP_RESERVED)
		return error;
	/* Wait for the captured original writer, including its error status. */
	error = bcmv3d_read_wait(&f->read, timeout);
	mutex_enter(&n->process_lock);
	f->preparing = false;
	mutex_enter(&n->lock);
	if (n->flip.fault != 0)
		error = scanout_fault(n, n->flip.fault);
	mutex_exit(&n->lock);
	if (error != 0)
		goto out;
	error = bcmv3d_mmu4_native_prepare_gem_read(n->sc->mmu4_native,
	    f->slot, f->object);
	if (error == 0) {
		mutex_enter(&n->lock);
		error = bcmv3d_flip_ready(&n->flip, f->slot);
		if (error == 0)
			n->prepared++;
		else
			(void)scanout_fault(n, error);
		mutex_exit(&n->lock);
	}
out:
	mutex_exit(&n->process_lock);
	return error;
}

int
bcmv3d_scanout_prepare(struct bcmv3d_scanout *n, struct drm_gem_object *obj,
    struct bcmv3d_scanout_frame **out)
{
	struct bcmv3d_scanout_frame *f;
	int error, cleanup;

	if (out == NULL)
		return EINVAL;
	error = bcmv3d_scanout_reserve(n, obj, &f);
	if (error != 0)
		return error;
	error = bcmv3d_scanout_finalize(f, 5 * hz);
	if (error != 0) {
		cleanup = bcmv3d_scanout_put(f);
		return cleanup != 0 ? cleanup : error;
	}
	*out = f;
	return 0;
}

int
bcmv3d_scanout_get(struct bcmv3d_scanout_frame *f)
{
	int error;

	if (f == NULL || f->owner == NULL)
		return EINVAL;
	if (f->slot >= BCMV3D_FLIP_GEM_SLOTS ||
	    f != &f->owner->frame[f->slot])
		return EPERM;
	if ((error = scanout_enter(f->owner)) != 0)
		return error;
	error = f->refs == 0 ? EINVAL : f->refs == UINT_MAX ? EOVERFLOW : 0;
	if (error == 0)
		f->refs++;
	mutex_exit(&f->owner->process_lock);
	return error;
}

int
bcmv3d_scanout_put(struct bcmv3d_scanout_frame *f)
{
	struct bcmv3d_scanout *n;
	enum bcmv3d_flip_state state;
	int error;

	if (f == NULL || f->owner == NULL)
		return EINVAL;
	n = f->owner;
	if (f->slot >= BCMV3D_FLIP_GEM_SLOTS || f != &n->frame[f->slot])
		return EPERM;
	if ((error = scanout_enter(n)) != 0)
		return error;
	if (f->refs == 0) {
		error = EINVAL;
		goto out;
	}
	if (--f->refs != 0)
		goto out;
	mutex_enter(&n->lock);
	state = n->flip.slot[f->slot].state;
	error = n->flip.fault;
	mutex_exit(&n->lock);
	if (error != 0)
		goto out;
	if ((state == BCMV3D_FLIP_RESERVED || state == BCMV3D_FLIP_READY) &&
	    !f->exposed) {
		error = scanout_cancel_unexposed(f);
		if (error != 0)
			goto out;
	}
	if (state == BCMV3D_FLIP_FREE || state == BCMV3D_FLIP_RESERVED ||
	    state == BCMV3D_FLIP_READY)
		scanout_drop_software(f);
out:
	mutex_exit(&n->process_lock);
	return error;
}

bool
bcmv3d_scanout_matches(struct bcmv3d_scanout_frame *f,
    struct drm_gem_object *obj)
{
	struct bcmv3d_scanout *n;
	enum bcmv3d_flip_state state;
	bool matches;

	if (f == NULL || f->owner == NULL || obj == NULL)
		return false;
	n = f->owner;
	if (f->slot >= BCMV3D_FLIP_GEM_SLOTS || f != &n->frame[f->slot])
		return false;
	if (scanout_enter(n) != 0)
		return false;
	mutex_enter(&n->lock);
	state = n->flip.slot[f->slot].state;
	matches = n->flip.fault == 0 && f->refs != 0 && f->object == obj &&
	    !f->preparing && (state == BCMV3D_FLIP_RESERVED ||
	    state == BCMV3D_FLIP_READY ||
	    state == BCMV3D_FLIP_PENDING || state == BCMV3D_FLIP_CURRENT);
	mutex_exit(&n->lock);
	mutex_exit(&n->process_lock);
	return matches;
}

static int
scanout_write(struct bcmv3d_scanout *n, uint32_t offset, uint32_t value)
{

	KASSERT(mutex_owned(&n->lock));
	KASSERT(!cpu_intr_p() && !cpu_softintr_p());
	if (n->flip.fault != 0)
		return n->flip.fault;
	if ((offset & 3U) != 0 ||
	    !((offset >= 0x4000 + 4 * 42 && offset <= 0x4000 + 4 * 72) ||
	    offset == n->control1 || offset == n->lptr ||
	    (n->variant == 0x53 ? offset == 0x38 :
	    offset == 0x108 || offset == 0x10c)))
		return scanout_fault(n, EACCES);
	if (bus_space_poke_4(n->bst, n->hvs, offset, value) != 0)
		return scanout_fault(n, EIO);
	bus_space_barrier(n->bst, n->hvs, 0, 0x1a000,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	return 0;
}

int
bcmv3d_scanout_submit(struct bcmv3d_scanout *n,
    struct bcmv3d_scanout_frame *f)
{
	struct bcmv3d_vblank_snapshot captured;
	unsigned int slot, i;
	uint32_t head, control, value;
	bool same;
	int error;

	if ((error = scanout_enter(n)) != 0)
		return error;
	slot = f == NULL ? BCMV3D_FLIP_BLANK : f->slot;
	if (slot >= BCMV3D_FLIP_SLOTS || (f != NULL &&
	    (slot >= BCMV3D_FLIP_GEM_SLOTS || f != &n->frame[slot] ||
	    f->owner != n || f->refs == 0 || f->preparing))) {
		error = EINVAL;
		goto out;
	}
	mutex_enter(&n->lock);
	same = n->flip.current_slot == (int)slot;
	error = n->flip.fault != 0 ? n->flip.fault :
	    n->flip.pending_slot >= 0 ? EBUSY :
	    n->flip.slot[slot].state != BCMV3D_FLIP_READY && !same ? EBUSY : 0;
	mutex_exit(&n->lock);
	if (error != 0)
		goto out;
	if (!same && f != NULL) {
		/* Exposure precedes every write that could publish this consumer. */
		error = bcmv3d_mmu4_native_expose_gem(n->sc->mmu4_native,
		    slot, f->object);
		if (error != 0)
			goto fault;
		f->exposed = true;
	}
	mutex_enter(&n->lock);
	/* Recheck after exposure and the unlocked native operation. */
	if (n->flip.fault != 0) {
		error = n->flip.fault;
		goto unlock;
	}
	error = bcmv3d_vblank_native_snapshot(n->sc->vblank_native, &captured);
	if (error == 0 && captured.first_error != 0)
		error = captured.first_error;
	if (error != 0) {
		error = scanout_fault(n, error);
		goto unlock;
	}
	/* Validate the actual active head; this read never creates an event. */
	if (bus_space_peek_4(n->bst, n->hvs,
	    n->variant == 0x53 ? 0x48 : 0x11c, &value) != 0) {
		error = scanout_fault(n, EIO);
		goto unlock;
	}
	head = n->flip.current_slot < 0 ? 32 :
	    bcmv3d_flip_head(n->flip.current_slot);
	if ((value & 0xfffU) != head) {
		error = scanout_fault(n, EPROTO);
		goto unlock;
	}
	error = bcmv3d_flip_submit_after(&n->flip, slot, captured.sequence);
	if (error != 0) {
		(void)scanout_fault(n, error);
		goto unlock;
	}
	if (same)
		goto unlock;
	head = bcmv3d_flip_head(slot);
	if (f != NULL) {
		for (i = 0; i < BCM2712_HVS_DLIST_WORDS; i++) {
			error = scanout_write(n, 0x4000 + 4 * (head + i),
			    f->list.words[i]);
			if (error != 0)
				goto unlock;
		}
		/* Only inactive SRAM can be verified; HVS mutates active context. */
		for (i = 0; i < BCM2712_HVS_DLIST_WORDS; i++) {
			if (bus_space_peek_4(n->bst, n->hvs,
			    0x4000 + 4 * (head + i), &value) != 0 ||
			    value != f->list.words[i]) {
				error = scanout_fault(n, EIO);
				goto unlock;
			}
		}
	} else {
		error = scanout_write(n, 0x4000 + 4 * head, 1U << 31);
		if (error != 0)
			goto unlock;
	}
	/* Black background also defines the reserved empty-list output. */
	if (n->variant == 0x53)
		error = scanout_write(n, 0x38, 0xff000000U);
	else {
		error = scanout_write(n, 0x108, 0);
		if (error == 0)
			error = scanout_write(n, 0x10c, 0xfff00000U);
	}
	if (error != 0)
		goto unlock;
	if (bus_space_peek_4(n->bst, n->hvs, n->control1, &control) != 0) {
		error = scanout_fault(n, EIO);
		goto unlock;
	}
	error = scanout_write(n, n->control1, control | 0x100U);
	if (error == 0)
		error = scanout_write(n, n->lptr, head);
	if (error == 0 && (bus_space_peek_4(n->bst, n->hvs, n->lptr,
	    &value) != 0 || (value & 0xfffU) != head))
		error = scanout_fault(n, EIO);
unlock:
	mutex_exit(&n->lock);
out:
	mutex_exit(&n->process_lock);
	return error;
fault:
	mutex_enter(&n->lock);
	(void)scanout_fault(n, error);
	mutex_exit(&n->lock);
	goto out;
}

int
bcmv3d_scanout_retirement_check(struct bcmv3d_scanout_frame *f,
    struct bcmv3d_mmu4_native *native, unsigned int slot,
    struct drm_gem_object *obj)
{
	struct bcmv3d_scanout *n;
	int error;

	if (f == NULL || f->owner == NULL || native == NULL || obj == NULL ||
	    slot >= BCMV3D_FLIP_GEM_SLOTS)
		return EINVAL;
	n = f->owner;
	if (n->sc->scanout != n || n->sc->mmu4_native != native ||
	    f != &n->frame[slot] || f->slot != slot || f->object != obj ||
	    !f->published || !f->exposed || f->preparing ||
	    !mutex_owned(&n->process_lock))
		return EPERM;
	mutex_enter(&n->lock);
	error = n->flip.fault != 0 ? n->flip.fault :
	    n->flip.owner != &n->flip ||
	    n->flip.slot[slot].state != BCMV3D_FLIP_RECLAIMING ||
	    n->flip.current_slot == (int)slot || n->flip.pending_slot == (int)slot ||
	    n->flip.slot[slot].retire_after == 0 ||
	    n->flip.frame_epoch < n->flip.slot[slot].retire_after ? EPERM : 0;
	mutex_exit(&n->lock);
	return error;
}

int
bcmv3d_scanout_cached_status(struct bcmv3d_scanout *n,
    struct bcmv3d_scanout_status *out)
{
	struct bcmv3d_scanout_status s;
	unsigned int i;

	if (n == NULL || out == NULL)
		return EINVAL;
	memset(&s, 0, sizeof(s));
	mutex_enter(&n->lock);
	s.version = 1;
	s.length = sizeof(s);
	s.sequence = n->flip.sequence;
	s.frame_epoch = n->flip.frame_epoch;
	s.latches = n->latches;
	s.prepared = n->prepared;
	s.retired = n->retired;
	s.current_slot = n->flip.current_slot;
	s.pending_slot = n->flip.pending_slot;
	s.first_error = n->flip.fault;
	s.worker_pending = n->work_pending;
	for (i = 0; i < BCMV3D_FLIP_SLOTS; i++)
		s.state[i] = n->flip.slot[i].state;
	mutex_exit(&n->lock);
	*out = s;
	return 0;
}
