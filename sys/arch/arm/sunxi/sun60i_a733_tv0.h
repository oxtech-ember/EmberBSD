/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; AI-assisted native TV0 mechanism. */
#ifndef _SUN60I_A733_TV0_H
#define _SUN60I_A733_TV0_H

#include <arm/sunxi/sun60i_a733_output.h>

enum sun60i_a733_tv0_space { A733_TV0_REGS, A733_VO1_REGS };
/* Explicit experiment: vendor writes zero; the manual has conflicting fields. */
enum sun60i_a733_tv0_profile { A733_TV0_EXPERIMENT_VENDOR_ZERO = 1 };

struct sun60i_a733_tv0_io {
	void *arg;
	int (*ready)(void *, const void *);
	int (*read)(void *, enum sun60i_a733_tv0_space, uint32_t, uint32_t *);
	int (*write)(void *, enum sun60i_a733_tv0_space, uint32_t, uint32_t);
};
struct sun60i_a733_tv0 {
	const struct sun60i_a733_tv0_io *io;
	const void *owner;
	int fault;
	bool written, routed, prepared, started;
	uint64_t sequence;
	/* One W0C underflow may be retried after a later clean IRQ. */
	uint64_t underflow_sequence, underflow_events;
	bool underflow_pending;
	/* Last observation's ordinary readbacks; bits 3, 5 and 6 are valid. */
	uint32_t observe_valid, observe_gint0, observe_gctl, observe_debug;
};

/* Native callbacks prove retained ownership and enforce apertures and widths. */
int sun60i_a733_tv0_route(struct sun60i_a733_tv0 *);
int sun60i_a733_tv0_prepare(struct sun60i_a733_tv0 *,
    const struct sun60i_a733_output_mode *, enum sun60i_a733_tv0_profile);
int sun60i_a733_tv0_start(struct sun60i_a733_tv0 *);
/* Stop timing/IRQs after the owner drains its handler. Retain prepared mode. */
int sun60i_a733_tv0_stop(struct sun60i_a733_tv0 *);
/* ENOENT means no new event. All other failures are sticky. */
int sun60i_a733_tv0_observe(struct sun60i_a733_tv0 *, uint64_t *);
/*
 * Interrupt mode. The vblank IRQ acknowledges the flag and counts events.
 * Check the started output and report a later count. One TV0 FIFO underflow
 * is W0C-cleared, then a later clean IRQ is required. A repeated underflow
 * and all other failures are sticky. ENOENT means no clean new event.
 */
int sun60i_a733_tv0_observe_count(struct sun60i_a733_tv0 *, uint64_t,
    uint64_t *);
#endif
