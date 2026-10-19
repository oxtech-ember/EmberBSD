/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Register facts: raspberrypi/linux a1a601d4857978d810175365550c4536701df4ed,
 * vc4_regs.h, vc4_crtc.c and vc4_hvs.c. This is an independent implementation.
 */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif
#include "bcm2712_display_quiesce.h"

#define PV_EN		0x00000001U
#define PV_FIFO_CLR	0x00000002U
#define PV_CLOCK	0x0000000cU
#define HVS_ENB		0x80000000U
#define HVS_RESET	0x40000000U
#define HVS_MODE	0x00006000U
#define POLL_STEPS	100
#define POLL_US		10

int
bcm2712_display_quiesce(const struct bcm2712_quiesce_ops *ops, void *arg,
    struct bcm2712_quiesce_status *s)
{
	struct bcm2712_quiesce_snapshot *b;
	struct bcm2712_quiesce_pipe *p;
	uint32_t ctrl, stat, stride, value;
	unsigned int i, channel = 2, poll;
	bool active[2], settle = false;
	int error;

	if (s == NULL || ops == NULL || ops->read == NULL ||
	    ops->write == NULL || ops->barrier == NULL || ops->delay == NULL)
		return EINVAL;
	if (s->attempted || s->complete || s->error != 0)
		return s->error != 0 ? s->error : EBUSY;
	s->attempted = true;
	b = &s->before;
#define READ(r, o, v) do { \
	error = ops->read(arg, (r), (o), &(v)); \
	if (error != 0) goto done; \
} while (0)
#define WRITE(r, o, v) do { \
	s->mutation_attempted = true; \
	s->pipe[channel].mutation_attempted = true; \
	error = ops->write(arg, (r), (o), (v)); \
	if (error != 0) goto done; \
	ops->barrier(arg, (r)); \
} while (0)
	/* VERSION is the first read. No guessed generation-specific offset. */
	READ(BCM2712_QUIESCE_HVS, 0, s->version);
	switch (s->version & 0xff) {
	case 0x53:
		ctrl = 0x30; stat = 0x44; stride = 0x20;
		break;
	case 0x54:
		ctrl = 0x100; stat = 0x118; stride = 0x40;
		break;
	default:
		error = EOPNOTSUPP;
		goto done;
	}
	READ(BCM2712_QUIESCE_HVS, 0x20, b->global);
	for (i = 0; i < 3; i++) {
		READ(BCM2712_QUIESCE_HVS, ctrl + i * stride, b->control[i]);
		READ(BCM2712_QUIESCE_HVS, stat + i * stride, b->status[i]);
	}
	for (i = 0; i < 2; i++) {
		READ(BCM2712_QUIESCE_PV0 + i, 0, b->pv_control[i]);
		READ(BCM2712_QUIESCE_PV0 + i, 4, b->pv_video[i]);
	}
	s->snapshot_valid = true;
	for (i = 0; i < 2; i++) {
		p = &s->pipe[i];
		p->pv_control = b->pv_control[i];
		p->pv_video = b->pv_video[i];
		p->control = b->control[i];
		p->status = b->status[i];
		active[i] = ((p->pv_control | p->pv_video) & PV_EN) != 0;
		p->pv_disabled = !active[i];
		p->hvs_disabled = (p->control & HVS_ENB) == 0 &&
		    (p->status & HVS_MODE) == 0;
	}
	/* Global firmware handoff covers both PVs, not a third HVS channel. */
	if ((b->control[2] & HVS_ENB) != 0 || (b->status[2] & HVS_MODE) != 0) {
		error = EBUSY;
		goto done;
	}
	for (channel = 0; channel < 2; channel++) {
		if ((s->pipe[channel].pv_control & PV_CLOCK) != 0) {
			error = EBUSY;
			goto done;
		}
	}
	/* Stop both video feeds before assuming either HVS-to-PV routing. */
	for (channel = 0; channel < 2; channel++) {
		p = &s->pipe[channel];
		if (!active[channel])
			continue;
		p->stage = BCM2712_QUIESCE_VIDEO;
		READ(BCM2712_QUIESCE_PV0 + channel, 4, value);
		WRITE(BCM2712_QUIESCE_PV0 + channel, 4, value & ~PV_EN);
		for (poll = 0;; poll++) {
			READ(BCM2712_QUIESCE_PV0 + channel, 4, p->pv_video);
			if ((p->pv_video & PV_EN) == 0)
				break;
			if (poll == POLL_STEPS) {
				error = ETIMEDOUT;
				goto done;
			}
			ops->delay(arg, POLL_US);
		}
		settle = true;
	}
	/* One interval starts only after the last active VIDEN has cleared. */
	if (settle)
		ops->delay(arg, 20000);
	for (channel = 0; channel < 2; channel++) {
		p = &s->pipe[channel];
		if (!active[channel])
			continue;
		p->stage = BCM2712_QUIESCE_CONTROL;
		READ(BCM2712_QUIESCE_PV0 + channel, 0, value);
		WRITE(BCM2712_QUIESCE_PV0 + channel, 0, value & ~PV_EN);
		READ(BCM2712_QUIESCE_PV0 + channel, 0, p->pv_control);
		if ((p->pv_control & PV_EN) != 0) {
			error = EIO;
			goto done;
		}
		WRITE(BCM2712_QUIESCE_PV0 + channel, 0,
		    p->pv_control | PV_FIFO_CLR);
		READ(BCM2712_QUIESCE_PV0 + channel, 0, p->pv_control);
		if ((p->pv_control & PV_EN) != 0) {
			error = EIO;
			goto done;
		}
		p->pv_disabled = true;
	}
	/* Both PVs are disabled before the first channel reset. */
	for (channel = 0; channel < 2; channel++) {
		p = &s->pipe[channel];
		if (!p->hvs_disabled) {
			p->stage = BCM2712_QUIESCE_CHANNEL;
			READ(BCM2712_QUIESCE_HVS, ctrl + channel * stride, value);
			WRITE(BCM2712_QUIESCE_HVS, ctrl + channel * stride,
			    value | HVS_RESET);
			/* Read again after RESET; preserve unrelated hardware changes. */
			READ(BCM2712_QUIESCE_HVS, ctrl + channel * stride, value);
			WRITE(BCM2712_QUIESCE_HVS, ctrl + channel * stride,
			    value & ~HVS_ENB);
			for (poll = 0;; poll++) {
				READ(BCM2712_QUIESCE_HVS, ctrl + channel * stride,
				    p->control);
				READ(BCM2712_QUIESCE_HVS, stat + channel * stride,
				    p->status);
				if ((p->control & HVS_ENB) == 0 &&
				    (p->status & HVS_MODE) == 0)
					break;
				if (poll == POLL_STEPS) {
					error = ETIMEDOUT;
					goto done;
				}
				ops->delay(arg, POLL_US);
			}
			p->hvs_disabled = true;
		}
		p->complete = true;
		p->stage = BCM2712_QUIESCE_COMPLETE;
	}
	s->complete = true;
	error = 0;
done:
	if (channel < 2 && error != 0 && s->pipe[channel].error == 0)
		s->pipe[channel].error = error;
	s->error = error;
	return error;
#undef READ
#undef WRITE
}
