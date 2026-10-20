/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted bounded check for the manual boot profile. No HVS access. */
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <linux/err.h>
#include <drm/drm_gem.h>
#include "bcmv3d_drv.h"
#include "bcmv3d_mmu4_native.h"
#include "bcmv3d_mmu4_check.h"

int
bcmv3d_mmu4_check_ranges(struct bcmv3d_device *sc,
    struct bcmv3d_mmu4_native *native, struct bcmv3d_mmu4_check_status *s)
{
	struct bcmv3d_bo_stats before, after;
	struct bcmv3d_bo *bo;
	unsigned int cycle, slot, owned = 0;
	int error = 0;

	if (sc == NULL || native == NULL || s == NULL)
		return EINVAL;
	if (s->attempted)
		return EALREADY;
	memset(s, 0, sizeof(*s));
	s->attempted = true;
	bcmv3d_bo_snapshot(sc, &before);
	s->before_bytes = before.reserved_bytes;
	if (before.reservations > UINT64_MAX - 6 ||
	    before.releases > UINT64_MAX - 6) {
		error = EOVERFLOW;
		goto done;
	}
	for (cycle = 0; cycle < 2; cycle++) {
		for (slot = 0; slot < 3; slot++) {
			bo = bcmv3d_bo_alloc(sc, 8294400);
			if (IS_ERR(bo)) {
				error = -PTR_ERR(bo);
				goto done;
			}
			error = bcmv3d_mmu4_native_stage_gem(native, slot,
			    &bo->cma.base);
			/* Stage owns its own reference, including uncertain backing. */
			drm_gem_object_put_unlocked(&bo->cma.base);
			if (error != 0)
				goto done;
			owned |= 1U << slot;
			s->staged++;
		}
		for (slot = 0; slot < 3; slot++) {
			error = bcmv3d_mmu4_native_publish_staged_gem(native, slot);
			if (error != 0)
				goto done;
			s->published++;
		}
		for (slot = 0; slot < 3; slot++) {
			error = bcmv3d_mmu4_native_unmap_gem(native, slot);
			if (error != 0)
				goto done;
			owned &= ~(1U << slot);
			s->unmapped++;
		}
		s->cycles++;
	}
done:
	/* Discard cannot remove a live PTE or free an exposed BO. No retry MMIO. */
	for (slot = 0; slot < 3; slot++) {
		if ((owned & (1U << slot)) != 0)
			(void)bcmv3d_mmu4_native_discard_staged_gem(native, slot);
	}
	bcmv3d_bo_snapshot(sc, &after);
	s->after_bytes = after.reserved_bytes;
	s->reservations = after.reservations - before.reservations;
	s->releases = after.releases - before.releases;
	if (error == 0 && (s->after_bytes != s->before_bytes ||
	    s->reservations != 6 || s->releases != 6 ||
	    after.failures != before.failures))
		error = EIO;
	s->first_error = error;
	s->complete = error == 0;
	return error;
}
