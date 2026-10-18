/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. */
#ifndef _SUN60I_A733_HDMI_PHY_H
#define _SUN60I_A733_HDMI_PHY_H

enum sun60i_a733_hdmi_space {
	A733_HDMI_CORE8, A733_HDMI_TOP32
};

struct sun60i_a733_hdmi_io {
	void *arg;
	/* Read-only proof of the current, already retained output owner. */
	int (*ready)(void *, const void *);
	int (*read)(void *, enum sun60i_a733_hdmi_space, uint32_t, uint32_t *);
	int (*write)(void *, enum sun60i_a733_hdmi_space, uint32_t, uint32_t);
	void (*delay)(void *, unsigned);
};

struct sun60i_a733_hdmi_plan {
	uint32_t dcxo_hz, pixel_hz, pll, pattern0;
	uint16_t mpll[3];
};

struct sun60i_a733_hdmi_phy {
	/* Zero this storage once, before registration. Do not reset a fault. */
	const struct sun60i_a733_hdmi_io *io;
	const void *owner;
	int fault;
	bool written, hpd_prepared, phy_prepared;
	struct sun60i_a733_hdmi_plan plan;
};

/* Callers serialize all calls. No callback may recurse into this core. */
int sun60i_a733_hdmi_plan(uint32_t, uint32_t,
    struct sun60i_a733_hdmi_plan *);
int sun60i_a733_hdmi_hpd_prepare(struct sun60i_a733_hdmi_phy *);
int sun60i_a733_hdmi_phy_prepare(struct sun60i_a733_hdmi_phy *,
    uint32_t, uint32_t);
int sun60i_a733_hdmi_phy_observe(struct sun60i_a733_hdmi_phy *);
#endif
