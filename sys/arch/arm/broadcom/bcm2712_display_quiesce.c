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
	uint32_t ctrl, stat, stride, value;
	unsigned int i;
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
	s->pv_control = b->pv_control[0];
	s->pv_video = b->pv_video[0];
	s->control = b->control[0];
	s->status = b->status[0];
	if (((b->pv_control[1] | b->pv_video[1]) & PV_EN) != 0 ||
	    (b->pv_control[0] & PV_CLOCK) != 0 ||
	    ((b->control[1] | b->control[2]) & HVS_ENB) != 0 ||
	    ((b->status[1] | b->status[2]) & HVS_MODE) != 0) {
		error = EBUSY;
		goto done;
	}
	/* A consistently disabled pipeline needs no register write or delay. */
	if (((s->pv_control | s->pv_video) & PV_EN) == 0 &&
	    (s->control & HVS_ENB) == 0 && (s->status & HVS_MODE) == 0)
		goto complete;

	READ(BCM2712_QUIESCE_PV0, 4, value);
	WRITE(BCM2712_QUIESCE_PV0, 4, value & ~PV_EN);
	for (i = 0;; i++) {
		READ(BCM2712_QUIESCE_PV0, 4, s->pv_video);
		if ((s->pv_video & PV_EN) == 0)
			break;
		if (i == POLL_STEPS) {
			error = ETIMEDOUT;
			goto done;
		}
		ops->delay(arg, POLL_US);
	}
	/* Keep the documented PV-to-HDMI FIFO settling interval. */
	ops->delay(arg, 20000);
	READ(BCM2712_QUIESCE_PV0, 0, value);
	WRITE(BCM2712_QUIESCE_PV0, 0, value & ~PV_EN);
	READ(BCM2712_QUIESCE_PV0, 0, s->pv_control);
	if ((s->pv_control & PV_EN) != 0) {
		error = EIO;
		goto done;
	}
	WRITE(BCM2712_QUIESCE_PV0, 0, s->pv_control | PV_FIFO_CLR);
	READ(BCM2712_QUIESCE_PV0, 0, s->pv_control);
	if ((s->pv_control & PV_EN) != 0) {
		error = EIO;
		goto done;
	}
	READ(BCM2712_QUIESCE_HVS, ctrl, value);
	WRITE(BCM2712_QUIESCE_HVS, ctrl, value | HVS_RESET);
	/* Use a new read after RESET. Preserve unrelated hardware changes. */
	READ(BCM2712_QUIESCE_HVS, ctrl, value);
	WRITE(BCM2712_QUIESCE_HVS, ctrl, value & ~HVS_ENB);
	for (i = 0;; i++) {
		READ(BCM2712_QUIESCE_HVS, ctrl, s->control);
		READ(BCM2712_QUIESCE_HVS, stat, s->status);
		if ((s->control & HVS_ENB) == 0 && (s->status & HVS_MODE) == 0)
			break;
		if (i == POLL_STEPS) {
			error = ETIMEDOUT;
			goto done;
		}
		ops->delay(arg, POLL_US);
	}
complete:
	s->complete = true;
	error = 0;
done:
	s->error = error;
	return error;
#undef READ
#undef WRITE
}
