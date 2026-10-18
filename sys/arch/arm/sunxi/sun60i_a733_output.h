/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; independent A733 output timing contract. */
#ifndef _SUN60I_A733_OUTPUT_H
#define _SUN60I_A733_OUTPUT_H

/* Progressive RGB888, one pixel per clock. All timing counts are pixels/lines. */
struct sun60i_a733_output_mode {
	uint32_t pixel_clock_hz;
	uint32_t hdisplay, hsync_start, hsync_end, htotal;
	uint32_t vdisplay, vsync_start, vsync_end, vtotal;
	bool hsync_positive, vsync_positive;
};

struct sun60i_a733_tcon_timing {
	/* TV0 offsets: 0x98, 0x9c, 0xa0, 0xa4, 0xa8 and 0x88. */
	uint32_t basic1, basic2, basic3, basic4, basic5, polarity;
};

/*
 * These functions perform no I/O. A timing is not a clock, PHY, EDID or owner
 * admission. Native prepare/start must use the same coordinator owner cookie.
 * Failure leaves the output unchanged. The caller serializes interrupt state.
 */
int sun60i_a733_tcon_timing_encode(const struct sun60i_a733_output_mode *,
    struct sun60i_a733_tcon_timing *);
/* Return ENOENT unless an enabled vblank is present in the supplied snapshot. */
int sun60i_a733_tcon_vblank_ack(uint32_t, uint32_t *);

/*
 * Native provider. All callbacks run under one sleep mutex. They must not
 * call these dispatch functions. Provider storage and ops have kernel lifetime.
 * The provider obtains CCU/PCK leases with this owner before any MMIO write.
 * get_mode may run DDC. It must not start scanout. prepare must check HPD again.
 * A failure is retained. There is no detach or retry after a provider failure.
 */
struct sun60i_a733_output_ops {
	int (*get_mode)(void *, const void *, struct sun60i_a733_output_mode *);
	int (*prepare)(void *, const void *, const struct sun60i_a733_output_mode *);
	int (*start)(void *, const void *);
	int (*observed_vblank)(void *, const void *, uint64_t *);
	void (*fault)(void *, const void *, int);
};
int sun60i_a733_output_register(int, const struct sun60i_a733_output_ops *, void *);
int sun60i_a733_output_get_mode(int, const void *,
    struct sun60i_a733_output_mode *);
int sun60i_a733_output_prepare(int, const void *,
    const struct sun60i_a733_output_mode *);
int sun60i_a733_output_start(int, const void *);
/* ENOENT means no new vblank. It does not latch a fault. */
int sun60i_a733_output_observed_vblank(int, const void *, uint64_t *);
int sun60i_a733_output_fault(int, const void *, int);

#endif
