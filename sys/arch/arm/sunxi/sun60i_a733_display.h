/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _SUN60I_A733_DISPLAY_H
#define _SUN60I_A733_DISPLAY_H

#define A733_DISPLAY_NCLOCK_REGS	15
#define A733_DISPLAY_NDOMAINS	2
#define A733_DISPLAY_NPOWER_REGS	3

struct sun60i_a733_display_reg {
	const char *name;
	uint32_t offset;
};

struct sun60i_a733_display_clocks {
	uint32_t sample[2][A733_DISPLAY_NCLOCK_REGS];
	uint32_t changed;
};

struct sun60i_a733_display_power {
	uint32_t sample[2][A733_DISPLAY_NDOMAINS][A733_DISPLAY_NPOWER_REGS];
	uint32_t changed;
};

typedef int (*sun60i_a733_display_read_fn)(void *, uint32_t, uint32_t *);
typedef int (*sun60i_a733_display_power_fn)(void *, u_int, uint32_t *,
    uint32_t *, uint32_t *);

extern const struct sun60i_a733_display_reg
    sun60i_a733_display_clock_regs[A733_DISPLAY_NCLOCK_REGS];
extern const u_int sun60i_a733_display_domains[A733_DISPLAY_NDOMAINS];
extern const char * const sun60i_a733_display_domain_names[A733_DISPLAY_NDOMAINS];

/* These snapshots never authorize access to DE, TCON, HDMI or PHY MMIO. */
int sun60i_a733_display_read_clocks(sun60i_a733_display_read_fn, void *,
    uint64_t, struct sun60i_a733_display_clocks *);
int sun60i_a733_display_read_power(sun60i_a733_display_power_fn, void *,
    struct sun60i_a733_display_power *);

#endif
