/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; AI-assisted native display arbitration contract. */
#ifndef _SUN60I_A733_DISPLAY_OWNER_H
#define _SUN60I_A733_DISPLAY_OWNER_H

#include <arm/sunxi/sun60i_a733_display.h>

/* Fixed resources. OTHER writers are counted but remain independent. */
enum sun60i_a733_display_resource {
	A733_DISPLAY_RES_DE_PLL, A733_DISPLAY_RES_DE0,
	A733_DISPLAY_RES_DE_SYS, A733_DISPLAY_RES_VO1,
	A733_DISPLAY_RES_TV0, A733_DISPLAY_RES_HDMI,
	A733_DISPLAY_RES_REF24, A733_DISPLAY_RES_PERIPH0,
	A733_DISPLAY_RES_AHB, A733_DISPLAY_RES_OTHER
};

enum sun60i_a733_display_ancestor {
	A733_DISPLAY_ANCESTOR_REF, A733_DISPLAY_ANCESTOR_PERIPH,
	A733_DISPLAY_ANCESTOR_PAT0, A733_DISPLAY_ANCESTOR_PAT1,
	A733_DISPLAY_ANCESTOR_AHB, A733_DISPLAY_ANCESTOR_GATE,
	A733_DISPLAY_ANCESTOR_STATUS, A733_DISPLAY_NANCESTORS
};

struct sun60i_a733_display_inputs {
	struct sun60i_a733_display_clocks clocks;
	struct sun60i_a733_display_power power;
	uint32_t ancestor[2][A733_DISPLAY_NANCESTORS];
	/* Two RTC reads before the CCU samples, two reads after them. */
	uint32_t dcxo[2][2];
};

struct sun60i_a733_display_rates {
	u_int reference_hz, periph400_hz, ahb_hz;
};

struct sun60i_a733_display_owner {
	/* Caller storage starts zeroed and lives for the provider lifetime. */
	const void *owner;
	u_int writers;
	bool checking, retained;
	int fault;
	struct sun60i_a733_display_inputs before;
	struct sun60i_a733_display_rates rates;
};

struct sun60i_a733_display_owner_status {
	bool reserved, retained;
	u_int writers;
	int fault;
	/* Last verified rates, not a fresh observation after a fault. */
	struct sun60i_a733_display_rates rates;
};

typedef int (*sun60i_a733_display_inputs_fn)(void *,
    struct sun60i_a733_display_inputs *);

/*
 * Pure core. The native caller serializes every operation with one mutex.
 * The callback must read fresh provider state under its reservations. It must
 * report access errors and quarantined PCK domains. No callback may write.
 * No API below performs MMIO, proves firmware exclusion, or authorizes DMA.
 */
int sun60i_a733_display_writer_enter(struct sun60i_a733_display_owner *,
    enum sun60i_a733_display_resource);
int sun60i_a733_display_writer_exit(struct sun60i_a733_display_owner *);
int sun60i_a733_display_reserve(struct sun60i_a733_display_owner *,
    const void *, sun60i_a733_display_inputs_fn, void *);
int sun60i_a733_display_release(struct sun60i_a733_display_owner *,
    const void *);
/* Mark before the first separately reviewed local DE0 write. Never release. */
int sun60i_a733_display_begin_de0(struct sun60i_a733_display_owner *,
    const void *, sun60i_a733_display_inputs_fn, void *);
int sun60i_a733_display_fault(struct sun60i_a733_display_owner *,
    const void *, int);
int sun60i_a733_display_owner_status(const struct sun60i_a733_display_owner *,
    struct sun60i_a733_display_owner_status *);
int sun60i_a733_display_running_ancestors(
    const struct sun60i_a733_display_inputs *,
    struct sun60i_a733_display_rates *);

#endif
