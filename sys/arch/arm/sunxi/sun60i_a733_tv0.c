/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; AI-assisted independent A733 TV0/VO1 register mechanism.
 * Primary field map: A733 User Manual V1.00 sections 15.2.9 and 15.2.10.
 * The explicit vendor-zero depth profile is an experiment, not an 8-bit proof.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_tv0.h>

static int
tv_fail(struct sun60i_a733_tv0 *s, int error)
{
	if (s->fault == 0 && error != 0)
		s->fault = error;
	return s->fault;
}

static int
tv_ready(struct sun60i_a733_tv0 *s)
{
	if (s == NULL)
		return EINVAL;
	if (s->fault != 0)
		return s->fault;
	if (s->owner == NULL || s->io == NULL || s->io->ready == NULL ||
	    s->io->read == NULL || s->io->write == NULL)
		return tv_fail(s, EINVAL);
	return tv_fail(s, s->io->ready(s->io->arg, s->owner));
}

static uint32_t
tv_read(struct sun60i_a733_tv0 *s, enum sun60i_a733_tv0_space space,
    uint32_t reg)
{
	uint32_t value = 0;

	if (s->fault == 0)
		tv_fail(s, s->io->read(s->io->arg, space, reg, &value));
	return value;
}

static void
tv_write(struct sun60i_a733_tv0 *s, enum sun60i_a733_tv0_space space,
    uint32_t reg, uint32_t value)
{
	if (s->fault != 0)
		return;
	/* The native lease is retained before even a faulting write attempt. */
	s->written = true;
	tv_fail(s, s->io->write(s->io->arg, space, reg, value));
}

static void
tv_update(struct sun60i_a733_tv0 *s, enum sun60i_a733_tv0_space space,
    uint32_t reg, uint32_t mask, uint32_t bits)
{
	uint32_t value;

	if ((bits & ~mask) != 0) {
		tv_fail(s, EINVAL);
		return;
	}
	value = tv_read(s, space, reg);
	tv_write(s, space, reg, (value & ~mask) | bits);
	value = tv_read(s, space, reg);
	if (s->fault == 0 && (value & mask) != bits)
		tv_fail(s, EIO);
}

int
sun60i_a733_tv0_route(struct sun60i_a733_tv0 *s)
{
	int error = tv_ready(s);

	if (error != 0)
		return error;
	if (s->routed)
		return tv_fail(s, EALREADY);
	/* VO1 selects PHY clock0 and HDMI0. Preserve the eDP and TV1 fields. */
	tv_update(s, A733_VO1_REGS, 0, UINT32_C(0x0000000a), 0);
	tv_update(s, A733_VO1_REGS, 0x20, UINT32_C(0x10100000),
	    UINT32_C(0x10100000));
	if (s->fault == 0)
		s->routed = true;
	return s->fault;
}

int
sun60i_a733_tv0_prepare(struct sun60i_a733_tv0 *s,
    const struct sun60i_a733_output_mode *mode,
    enum sun60i_a733_tv0_profile profile)
{
	struct sun60i_a733_tcon_timing t;
	uint32_t start_delay, value;
	int error = tv_ready(s);

	if (error != 0)
		return error;
	if (!s->routed || s->prepared)
		return tv_fail(s, EINVAL);
	if (profile != A733_TV0_EXPERIMENT_VENDOR_ZERO)
		return tv_fail(s, EOPNOTSUPP);
	error = sun60i_a733_tcon_timing_encode(mode, &t);
	if (error != 0)
		return tv_fail(s, error);
	if (mode->vtotal - mode->vdisplay <= 5)
		return tv_fail(s, ERANGE);
	start_delay = MIN(mode->vtotal - mode->vdisplay - 5, 31);
	value = tv_read(s, A733_TV0_REGS, 0x90);
	if (s->fault == 0 && (value & UINT32_C(0x80000000)) != 0)
		return tv_fail(s, EBUSY);
	/* Disable all local sync/data outputs while programming the timing. */
	tv_update(s, A733_TV0_REGS, 0x8c, UINT32_C(0x0f000000),
	    UINT32_C(0x0f000000));
	tv_update(s, A733_TV0_REGS, 0x338, UINT32_MAX, UINT32_MAX);
	tv_update(s, A733_TV0_REGS, 0x33c, UINT32_C(0xffff0000),
	    UINT32_C(0xffff0000));
	/* GINT0 flags are W0C. Clear them and disable local interrupt sources. */
	tv_write(s, A733_TV0_REGS, 4, 0);
	value = tv_read(s, A733_TV0_REGS, 4);
	if (s->fault == 0 && (value & UINT32_C(0x5f005f00)) != 0)
		tv_fail(s, EIO);
	/* Clear old stopped-state errors before the first frame. */
	tv_write(s, A733_TV0_REGS, 0xfc, 0);
	value = tv_read(s, A733_TV0_REGS, 0xfc);
	if (s->fault == 0 && (value & UINT32_C(0x60000000)) != 0)
		tv_fail(s, EIO);
	/* GCTL bit31 is reserved on A733. It is never used as an enable. */
	tv_update(s, A733_TV0_REGS, 0, UINT32_C(0x31), 0);
	tv_update(s, A733_TV0_REGS, 0x40, 1, 0);
	tv_update(s, A733_TV0_REGS, 0x90, UINT32_C(0x000ffff0),
	    start_delay << 4);
	tv_update(s, A733_TV0_REGS, 0x98, UINT32_C(0x8001ffff), t.basic1);
	tv_update(s, A733_TV0_REGS, 0x9c, UINT32_C(0x7fff7fff), t.basic2);
	tv_update(s, A733_TV0_REGS, 0xa0, UINT32_C(0xffff7fff), t.basic3);
	tv_update(s, A733_TV0_REGS, 0xa4, UINT32_C(0x00007fff), t.basic4);
	tv_update(s, A733_TV0_REGS, 0xa8, UINT32_C(0x0fff0fff), t.basic5);
	tv_update(s, A733_TV0_REGS, 0x88, UINT32_C(0x0f000000), t.polarity);
	tv_update(s, A733_TV0_REGS, 0x300, UINT32_C(0x80000000), 0);
	tv_update(s, A733_TV0_REGS, 0x330, UINT32_MAX, 0);
	tv_update(s, A733_TV0_REGS, 0x334, UINT32_C(0xffff0000), 0);
	/* Manual PIXELDEPTH and VRR descriptions overlap at bit0. */
	tv_update(s, A733_TV0_REGS, 0x340, UINT32_C(0xc0ffff77), 0);
	if (s->fault == 0)
		s->prepared = true;
	return s->fault;
}

int
sun60i_a733_tv0_start(struct sun60i_a733_tv0 *s)
{
	uint32_t value;
	int error = tv_ready(s);

	if (error != 0)
		return error;
	if (!s->prepared || s->started)
		return tv_fail(s, EINVAL);
	value = tv_read(s, A733_TV0_REGS, 0x90);
	if (s->fault == 0 && (value & UINT32_C(0x80000000)) != 0)
		return tv_fail(s, EBUSY);
	/* Clear a stale vblank before start. Preserve other W0C flags. */
	tv_write(s, A733_TV0_REGS, 4, UINT32_C(0x40001f00));
	value = tv_read(s, A733_TV0_REGS, 4);
	if (s->fault == 0 && (value & UINT32_C(0x5f004000)) != UINT32_C(0x40000000))
		tv_fail(s, EIO);
	tv_update(s, A733_TV0_REGS, 0x338, UINT32_MAX, 0);
	tv_update(s, A733_TV0_REGS, 0x33c, UINT32_C(0xffff0000), 0);
	tv_update(s, A733_TV0_REGS, 0x8c, UINT32_C(0x0f000000), 0);
	tv_update(s, A733_TV0_REGS, 0x90, UINT32_C(0x80000000),
	    UINT32_C(0x80000000));
	if (s->fault == 0)
		s->started = true;
	return s->fault;
}

int
sun60i_a733_tv0_observe(struct sun60i_a733_tv0 *s, uint64_t *sequence)
{
	uint32_t value, ack;
	int error = tv_ready(s);

	if (error != 0)
		return error;
	if (!s->started || sequence == NULL)
		return tv_fail(s, EINVAL);
	value = tv_read(s, A733_TV0_REGS, 0x90);
	if (s->fault == 0 && (value & UINT32_C(0x80000000)) == 0)
		tv_fail(s, EIO);
	value = tv_read(s, A733_TV0_REGS, 0xfc);
	if (s->fault == 0 && (value & UINT32_C(0x60000000)) != 0)
		tv_fail(s, EIO);
	value = tv_read(s, A733_TV0_REGS, 4);
	if (s->fault != 0)
		return s->fault;
	if ((value & UINT32_C(0x5f000000)) != UINT32_C(0x40000000))
		return tv_fail(s, EIO);
	error = sun60i_a733_tcon_vblank_ack(value, &ack);
	if (error != 0)
		return error == ENOENT ? error : tv_fail(s, error);
	if (s->sequence == UINT64_MAX)
		return tv_fail(s, EOVERFLOW);
	tv_write(s, A733_TV0_REGS, 4, ack);
	value = tv_read(s, A733_TV0_REGS, 4);
	if (s->fault == 0 && (value & UINT32_C(0x5f004000)) !=
	    (ack & UINT32_C(0x5f000000)))
		tv_fail(s, EIO);
	if (s->fault != 0)
		return s->fault;
	*sequence = ++s->sequence;
	return 0;
}

int
sun60i_a733_tv0_observe_count(struct sun60i_a733_tv0 *s, uint64_t count,
    uint64_t *sequence)
{
	uint32_t value;
	int error = tv_ready(s);

	if (error != 0)
		return error;
	if (!s->started || sequence == NULL)
		return tv_fail(s, EINVAL);
	value = tv_read(s, A733_TV0_REGS, 0x90);
	if (s->fault == 0 && (value & UINT32_C(0x80000000)) == 0)
		tv_fail(s, EIO);
	value = tv_read(s, A733_TV0_REGS, 0xfc);
	if (s->fault == 0 && (value & UINT32_C(0x60000000)) != 0)
		tv_fail(s, EIO);
	value = tv_read(s, A733_TV0_REGS, 4);
	if (s->fault != 0)
		return s->fault;
	/* The IRQ owns the W0C flags. Only the enable set is checked here. */
	if ((value & UINT32_C(0x5f000000)) != UINT32_C(0x40000000))
		return tv_fail(s, EIO);
	if (count < s->sequence)
		return tv_fail(s, EIO);
	if (count == s->sequence)
		return ENOENT;
	s->sequence = count;
	*sequence = count;
	return 0;
}
