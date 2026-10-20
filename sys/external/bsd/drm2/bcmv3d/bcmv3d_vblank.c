/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* AI-assisted independent PV0 event mechanism. Numeric facts: Raspberry Pi
 * Linux a1a601d4857978d810175365550c4536701df4ed, vc4_regs.h/vc4_crtc.c.
 * No GPL implementation is copied here.
 */
#include <sys/param.h>
#include <sys/errno.h>
#include "bcmv3d_vblank.h"

#define PV_INTEN 0x24U
#define PV_INTSTAT 0x28U
#define PV_VFP_START 0x80U

static int
vblank_fault(struct bcmv3d_vblank *s, int error)
{

	if (s->fault == 0)
		s->fault = error > 0 ? error : EIO;
	return s->fault;
}

static int
vblank_read(struct bcmv3d_vblank *s, enum bcmv3d_vblank_window w,
    uint32_t offset, uint32_t *value)
{
	int error;

	s->last_window = w;
	s->last_offset = offset;
	s->raw_valid = false;
	s->reject_mask = s->reject_expected = 0;
	error = s->io->read(s->arg, w, offset, value);
	if (error != 0)
		return vblank_fault(s, error);
	s->last_raw = *value;
	s->raw_valid = true;
	return 0;
}

static int
vblank_write(struct bcmv3d_vblank *s, uint32_t offset, uint32_t value)
{
	int error;

	s->last_window = BCMV3D_VBLANK_PV0;
	s->last_offset = offset;
	s->raw_valid = false;
	s->reject_mask = s->reject_expected = 0;
	error = s->io->write(s->arg, BCMV3D_VBLANK_PV0, offset, value);
	if (error != 0)
		return vblank_fault(s, error);
	s->io->barrier(s->arg, BCMV3D_VBLANK_PV0);
	return 0;
}

static int
vblank_expect(struct bcmv3d_vblank *s, uint32_t value, uint32_t mask,
    uint32_t expected, int error)
{

	s->reject_mask = mask;
	s->reject_expected = expected;
	return (value & mask) == expected ? 0 : vblank_fault(s, error);
}

static int
vblank_sample(struct bcmv3d_vblank *s, bool initial)
{
	uint32_t control, status, active;
	int error;

	error = vblank_read(s, BCMV3D_VBLANK_HVS, s->control_offset, &control);
	if (error == 0)
		error = vblank_expect(s, control, 0x80000000U, 0x80000000U, EBUSY);
	if (error == 0)
		error = vblank_read(s, BCMV3D_VBLANK_HVS, s->status_offset, &status);
	if (error == 0)
		error = vblank_expect(s, status, 0x6000U, 0x4000U, EBUSY);
	if (error == 0)
		error = vblank_read(s, BCMV3D_VBLANK_HVS, s->active_offset, &active);
	if (error == 0 && initial)
		error = vblank_expect(s, active, 0xfffU, 32, EBUSY);
	if (error != 0)
		return error;
	s->channel_status = status;
	s->active_list = active & 0xfffU;
	s->frame_count = (status >> 16) & 63U;
	return 0;
}

int
bcmv3d_vblank_prepare(struct bcmv3d_vblank *s,
    const struct bcmv3d_vblank_io *io, void *arg, uint32_t variant)
{
	uint32_t value;
	int error;

	if (s == NULL || io == NULL || io->read == NULL || io->write == NULL ||
	    io->barrier == NULL)
		return EINVAL;
	if (s->owner != NULL || s->attempted || s->fault != 0)
		return s->fault != 0 ? s->fault : EBUSY;
	if (variant != 0x53 && variant != 0x54)
		return EOPNOTSUPP;
	s->owner = s;
	s->io = io;
	s->arg = arg;
	s->variant = variant;
	s->attempted = true;
	s->control_offset = variant == 0x53 ? 0x30 : 0x100;
	s->status_offset = variant == 0x53 ? 0x44 : 0x118;
	s->active_offset = variant == 0x53 ? 0x48 : 0x11c;
	error = vblank_read(s, BCMV3D_VBLANK_HVS, 0, &value);
	if (error != 0)
		return error;
	error = vblank_expect(s, value, 0xffU, variant, ENODEV);
	if (error != 0)
		return error;
	error = vblank_sample(s, true);
	if (error != 0)
		return error;
	error = vblank_read(s, BCMV3D_VBLANK_PV0, 0, &value);
	if (error != 0)
		return error;
	error = vblank_expect(s, value, 1U, 1U, EBUSY);
	if (error != 0)
		return error;
	error = vblank_read(s, BCMV3D_VBLANK_PV0, 4, &value);
	if (error != 0)
		return error;
	error = vblank_expect(s, value, 1U, 1U, EBUSY);
	if (error != 0)
		return error;
	error = vblank_read(s, BCMV3D_VBLANK_PV0, PV_INTEN, &value);
	if (error != 0)
		return error;
	/* The retained owner owns all PV0 sources, including an inherited mask. */
	error = vblank_expect(s, value, ~0x3ffU, 0, EOPNOTSUPP);
	if (error == 0)
		error = vblank_write(s, PV_INTEN, 0);
	if (error == 0)
		error = vblank_read(s, BCMV3D_VBLANK_PV0, PV_INTEN, &value);
	if (error == 0)
		error = vblank_expect(s, value, UINT32_MAX, 0, EIO);
	if (error != 0)
		return error;
	error = vblank_write(s, PV_INTSTAT, PV_VFP_START);
	if (error != 0)
		return error;
	s->prepared = true;
	return 0;
}

int
bcmv3d_vblank_arm(struct bcmv3d_vblank *s)
{
	uint32_t value;
	int error;

	if (s == NULL)
		return EINVAL;
	if (s->owner != s)
		return EPERM;
	if (s->fault != 0)
		return s->fault;
	if (!s->prepared || s->armed)
		return EBUSY;
	/* Native serialization prevents dispatch until this arm attempt returns. */
	s->armed = true;
	error = vblank_write(s, PV_INTEN, PV_VFP_START);
	if (error == 0)
		error = vblank_read(s, BCMV3D_VBLANK_PV0, PV_INTEN, &value);
	if (error != 0)
		return error;
	return vblank_expect(s, value, UINT32_MAX, PV_VFP_START, EIO);
}

int
bcmv3d_vblank_irq(struct bcmv3d_vblank *s, bool *handled)
{
	uint32_t pending;
	int error;

	if (handled == NULL)
		return EINVAL;
	*handled = false;
	if (s == NULL)
		return EINVAL;
	if (s->owner != s)
		return EPERM;
	if (s->fault != 0)
		return s->fault;
	if (!s->prepared || !s->armed)
		return EBUSY;
	error = vblank_read(s, BCMV3D_VBLANK_PV0, PV_INTSTAT, &pending);
	if (error != 0)
		return error;
	if ((pending & PV_VFP_START) == 0) {
		s->spurious++;
		return 0;
	}
	error = vblank_write(s, PV_INTSTAT, PV_VFP_START);
	if (error == 0)
		error = vblank_sample(s, false);
	if (error != 0)
		return error;
	s->sequence++;
	*handled = true;
	return 0;
}
