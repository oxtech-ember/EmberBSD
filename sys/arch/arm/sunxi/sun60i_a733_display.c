/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Read-only A733 display provider snapshots.
 * Register facts: A733 User Manual V1.00, sections 4.1.6.61, 82, 83,
 * 104, 105, 113, 225, 226, 230, 231, 232, 235, 237, 256 and 265.
 * PCK domain IDs: the existing dual-licensed A733 power binding.
 * No GPL driver implementation is copied. See ember/boot/a733-display-observer.md.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_display.h>

const struct sun60i_a733_display_reg
sun60i_a733_display_clock_regs[A733_DISPLAY_NCLOCK_REGS] = {
	{ "PLL_DE_CTRL", 0x02e0 },
	{ "AHB_MASTER_GATE", 0x05c0 },
	{ "MBUS_MASTER_GATE", 0x05e0 },
	{ "DE0_CLK", 0x0a00 },
	{ "DE0_BGR", 0x0a04 },
	{ "DE_SYS_BGR", 0x0a74 },
	{ "TCONTV0_BGR", 0x1604 },
	{ "TCONTV1_BGR", 0x160c },
	{ "HDMI_TV_CLK", 0x1684 },
	{ "HDMI_BGR", 0x168c },
	{ "HDMI_SFR_CLK", 0x1690 },
	{ "DPSS_TOP1_BGR", 0x16cc },
	{ "VIDEO_OUT1_BGR", 0x16ec },
	{ "DEPLL_GATE_EN", 0x1928 },
	{ "DEPLL_GATE_STAT", 0x19a8 }
};

const u_int sun60i_a733_display_domains[A733_DISPLAY_NDOMAINS] = { 1, 10 };
const char * const sun60i_a733_display_domain_names[A733_DISPLAY_NDOMAINS] = {
	"DE_SYS", "VO1"
};

int
sun60i_a733_display_read_clocks(sun60i_a733_display_read_fn read, void *arg,
    uint64_t size, struct sun60i_a733_display_clocks *out)
{
	struct sun60i_a733_display_clocks state = { 0 };

	if (read == NULL || out == NULL)
		return EINVAL;
	/* Validate the entire aperture before the first provider access. */
	for (u_int r = 0; r < A733_DISPLAY_NCLOCK_REGS; r++) {
		if (size < (uint64_t)sun60i_a733_display_clock_regs[r].offset + 4)
			return EINVAL;
	}
	for (u_int s = 0; s < 2; s++) {
		for (u_int r = 0; r < A733_DISPLAY_NCLOCK_REGS; r++) {
			const int error = read(arg, sun60i_a733_display_clock_regs[r].offset,
			    &state.sample[s][r]);
			if (error != 0)
				return error;
		}
	}
	for (u_int r = 0; r < A733_DISPLAY_NCLOCK_REGS; r++) {
		if (state.sample[0][r] != state.sample[1][r])
			state.changed |= __BIT(r);
	}
	*out = state;
	return 0;
}

int
sun60i_a733_display_read_power(sun60i_a733_display_power_fn read, void *arg,
    struct sun60i_a733_display_power *out)
{
	struct sun60i_a733_display_power state = { 0 };

	if (read == NULL || out == NULL)
		return EINVAL;
	for (u_int s = 0; s < 2; s++) {
		for (u_int d = 0; d < A733_DISPLAY_NDOMAINS; d++) {
			const int error = read(arg, sun60i_a733_display_domains[d],
			    &state.sample[s][d][0], &state.sample[s][d][1],
			    &state.sample[s][d][2]);
			if (error != 0)
				return error;
		}
	}
	for (u_int d = 0; d < A733_DISPLAY_NDOMAINS; d++) {
		for (u_int r = 0; r < A733_DISPLAY_NPOWER_REGS; r++) {
			if (state.sample[0][d][r] != state.sample[1][d][r])
				state.changed |= __BIT(d * A733_DISPLAY_NPOWER_REGS + r);
		}
	}
	*out = state;
	return 0;
}
