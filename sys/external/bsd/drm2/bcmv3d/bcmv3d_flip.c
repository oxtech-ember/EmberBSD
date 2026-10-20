/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted HVS list/frame protocol. Numeric facts: Raspberry Pi Linux
 * a1a601d4857978d810175365550c4536701df4ed, vc4_hvs.c and vc4_crtc.c.
 * No GPL implementation is copied here.
 */
#include <sys/param.h>
#include <sys/errno.h>
#include "bcmv3d_flip.h"

static int
flip_check(struct bcmv3d_flip *s)
{

	if (s == NULL)
		return EINVAL;
	if (s->owner != s)
		return EPERM;
	if (s->fault != 0)
		return s->fault;
	return s->initialized ? 0 : ENXIO;
}

uint32_t
bcmv3d_flip_head(unsigned int slot)
{

	return slot < BCMV3D_FLIP_SLOTS ? 42U + 10U * slot : UINT32_MAX;
}

int
bcmv3d_flip_fail(struct bcmv3d_flip *s, int error)
{

	if (s == NULL || error <= 0)
		return EINVAL;
	if (s->owner != s)
		return EPERM;
	if (s->fault == 0)
		s->fault = error;
	return s->fault;
}

int
bcmv3d_flip_init(struct bcmv3d_flip *s, const struct bcmv3d_flip_event *e)
{

	if (s == NULL || e == NULL)
		return EINVAL;
	if (s->owner != NULL || s->initialized || s->fault != 0)
		return EBUSY;
	if (e->first_error != 0 || e->active_list != 32 || e->frame_count >= 64)
		return EINVAL;
	s->owner = s;
	s->sequence = e->sequence;
	s->frame_count = e->frame_count;
	s->current_slot = s->pending_slot = -1;
	s->slot[BCMV3D_FLIP_BLANK].state = BCMV3D_FLIP_READY;
	s->initialized = true;
	return 0;
}

int
bcmv3d_flip_ready(struct bcmv3d_flip *s, unsigned int slot)
{
	int error;

	if ((error = flip_check(s)) != 0)
		return error;
	if (slot >= BCMV3D_FLIP_GEM_SLOTS)
		return EINVAL;
	if (s->slot[slot].state != BCMV3D_FLIP_FREE)
		return EBUSY;
	s->slot[slot].state = BCMV3D_FLIP_READY;
	return 0;
}

int
bcmv3d_flip_cancel(struct bcmv3d_flip *s, unsigned int slot)
{
	int error;

	if ((error = flip_check(s)) != 0)
		return error;
	if (slot >= BCMV3D_FLIP_GEM_SLOTS)
		return EINVAL;
	if (s->slot[slot].state != BCMV3D_FLIP_READY)
		return EBUSY;
	s->slot[slot].state = BCMV3D_FLIP_FREE;
	return 0;
}

int
bcmv3d_flip_submit_after(struct bcmv3d_flip *s, unsigned int slot,
    uint64_t captured_sequence)
{
	int error;

	if ((error = flip_check(s)) != 0)
		return error;
	if (slot >= BCMV3D_FLIP_SLOTS)
		return EINVAL;
	if (s->pending_slot >= 0)
		return EBUSY;
	if (s->slot[slot].state != BCMV3D_FLIP_READY &&
	    ((int)slot != s->current_slot ||
	    s->slot[slot].state != BCMV3D_FLIP_CURRENT))
		return EBUSY;
	if (captured_sequence < s->sequence)
		return EINVAL;
	s->pending_slot = slot;
	s->queued_sequence = captured_sequence;
	if ((int)slot != s->current_slot)
		s->slot[slot].state = BCMV3D_FLIP_PENDING;
	return 0;
}

int
bcmv3d_flip_submit(struct bcmv3d_flip *s, unsigned int slot)
{

	return bcmv3d_flip_submit_after(s, slot, s == NULL ? 0 : s->sequence);
}

int
bcmv3d_flip_observe(struct bcmv3d_flip *s, const struct bcmv3d_flip_event *e,
    struct bcmv3d_flip_update *out)
{
	struct bcmv3d_flip_update result = { 0 };
	uint64_t gap;
	uint32_t delta, current_head, pending_head;
	unsigned int i;
	int old, error;

	if (e == NULL || out == NULL)
		return EINVAL;
	if ((error = flip_check(s)) != 0)
		return error;
	if (e->first_error != 0)
		return bcmv3d_flip_fail(s,
		    e->first_error > 0 ? e->first_error : EIO);
	/* Callbacks leave the native IRQ mutex and may reach another CPU late. */
	if (e->sequence <= s->sequence) {
		*out = result;
		return 0;
	}
	gap = e->sequence - s->sequence;
	if (e->frame_count >= 64 || gap > 31)
		return bcmv3d_flip_fail(s, EOVERFLOW);
	delta = (e->frame_count - s->frame_count) & 63U;
	if (delta >= 32 || s->frame_epoch > UINT64_MAX - delta)
		return bcmv3d_flip_fail(s, EOVERFLOW);
	current_head = s->current_slot < 0 ? 32 : bcmv3d_flip_head(s->current_slot);
	pending_head = s->pending_slot < 0 ? UINT32_MAX :
	    bcmv3d_flip_head(s->pending_slot);
	if (e->active_list != current_head && e->active_list != pending_head)
		return bcmv3d_flip_fail(s, EPROTO);
	s->sequence = e->sequence;
	s->frame_count = e->frame_count;
	s->frame_epoch += delta;
	if (s->pending_slot >= 0 && e->sequence > s->queued_sequence &&
	    e->active_list == pending_head) {
		old = s->current_slot;
		if (old >= 0 && old != s->pending_slot) {
			if (s->frame_epoch == UINT64_MAX)
				return bcmv3d_flip_fail(s, EOVERFLOW);
			s->slot[old].retire_after = s->frame_epoch + 1;
			s->slot[old].state = BCMV3D_FLIP_RETIRING;
		}
		s->current_slot = s->pending_slot;
		s->slot[s->current_slot].state = BCMV3D_FLIP_CURRENT;
		s->pending_slot = -1;
		s->latch_sequence = e->sequence;
		result.latched = true;
	}
	for (i = 0; i < BCMV3D_FLIP_SLOTS; i++) {
		if (s->slot[i].state == BCMV3D_FLIP_RETIRING &&
		    s->frame_epoch >= s->slot[i].retire_after) {
			s->slot[i].state = BCMV3D_FLIP_RETIRED;
			result.retired_mask |= 1U << i;
		}
	}
	*out = result;
	return 0;
}

int
bcmv3d_flip_claim_retired(struct bcmv3d_flip *s, unsigned int slot)
{
	int error;

	if ((error = flip_check(s)) != 0)
		return error;
	if (slot >= BCMV3D_FLIP_SLOTS)
		return EINVAL;
	if (s->slot[slot].state != BCMV3D_FLIP_RETIRED ||
	    s->current_slot == (int)slot || s->pending_slot == (int)slot)
		return EBUSY;
	s->slot[slot].state = BCMV3D_FLIP_RECLAIMING;
	return 0;
}

int
bcmv3d_flip_reclaimed(struct bcmv3d_flip *s, unsigned int slot)
{
	int error;

	if ((error = flip_check(s)) != 0)
		return error;
	if (slot >= BCMV3D_FLIP_SLOTS)
		return EINVAL;
	if (s->slot[slot].state != BCMV3D_FLIP_RECLAIMING)
		return EBUSY;
	s->slot[slot].state = slot == BCMV3D_FLIP_BLANK ?
	    BCMV3D_FLIP_READY : BCMV3D_FLIP_FREE;
	s->slot[slot].retire_after = 0;
	return 0;
}
