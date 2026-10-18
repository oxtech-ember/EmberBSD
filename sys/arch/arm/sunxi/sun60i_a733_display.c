/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Read-only A733 display provider snapshots.
 * Register facts: A733 User Manual V1.00, sections 4.1.6.61, 82, 83,
 * 63, 64, 74, 104, 105, 113-115, 119, 123, 225, 226, 230-232, 235,
 * 237, 244, 256 and 265.
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
	{ "DEPLL_GATE_STAT", 0x19a8 },
	{ "PLL_DE_PAT0", 0x02e8 },
	{ "PLL_DE_PAT1", 0x02ec },
	{ "NSI_CLK", 0x0580 },
	{ "VE_ENC0_CLK", 0x0a80 },
	{ "VE_DEC_CLK", 0x0a88 },
	{ "NPU_CLK", 0x0b00 },
	{ "DRAM0_CLK", 0x0c00 },
	{ "CSI_CLK", 0x1840 }
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

int
sun60i_a733_display_decode_clocks(const struct sun60i_a733_display_clocks *in,
    struct sun60i_a733_display_clock_facts *out)
{
	/* Last selector is inclusive. 0xff means no DE PLL parent. */
	static const struct {
		u_int reg;
		uint8_t last, de3x, de4x;
	} muxes[A733_DISPLAY_NCONSUMERS] = {
		{ A733_DISPLAY_DE0, 6, 0, 1 },
		{ A733_DISPLAY_NSI, 5, 5, 0xff },
		{ A733_DISPLAY_VE_ENC0, 6, 5, 0xff },
		{ A733_DISPLAY_VE_DEC, 6, 5, 0xff },
		{ A733_DISPLAY_NPU, 6, 6, 0xff },
		{ A733_DISPLAY_DRAM0, 4, 3, 0xff },
		{ A733_DISPLAY_CSI, 6, 0xff, 1 },
		{ A733_DISPLAY_HDMI_TV, 3, 0xff, 0xff },
		{ A733_DISPLAY_HDMI_SFR, 1, 0xff, 0xff }
	};
	static const u_int buses[] = {
		A733_DISPLAY_DE0_BUS, A733_DISPLAY_TV0_BUS,
		A733_DISPLAY_TV1_BUS, A733_DISPLAY_HDMI_BUS,
		A733_DISPLAY_DPSS_TOP1_BUS
	};
	static const u_int resets[] = {
		A733_DISPLAY_DE0_BUS, A733_DISPLAY_DE_SYS_RESET,
		A733_DISPLAY_TV0_BUS, A733_DISPLAY_TV1_BUS,
		A733_DISPLAY_DPSS_TOP1_BUS, A733_DISPLAY_VO1_RESET
	};
	struct sun60i_a733_display_clock_facts state = { 0 };
	const uint32_t *v;

	if (in == NULL || out == NULL)
		return EINVAL;
	/* Do not trust a caller-supplied zero changed mask on its own. */
	if (in->changed != 0 ||
	    memcmp(in->sample[0], in->sample[1], sizeof(in->sample[0])) != 0)
		return EBUSY;
	v = in->sample[0];
	state.pll_control = v[A733_DISPLAY_PLL_DE] & 0xfc77ff02;
	state.pll_pattern0 = v[A733_DISPLAY_PLL_DE_PAT0] & __BIT(31);
	state.pll_pattern1 = v[A733_DISPLAY_PLL_DE_PAT1] & 0x8f100000;
	state.pll_gate_control = v[A733_DISPLAY_DEPLL_GATE] & 0x00030003;
	state.pll_gate_status = v[A733_DISPLAY_DEPLL_STATUS] & 0x00030000;
	for (u_int n = 0; n < __arraycount(buses); n++) {
		if ((v[buses[n]] & __BIT(0)) != 0)
			state.bus_gates |= __BIT(buses[n]);
	}
	for (u_int n = 0; n < __arraycount(resets); n++) {
		if ((v[resets[n]] & __BIT(16)) != 0)
			state.resets_deasserted |= __BIT(resets[n]);
	}
	state.hdmi_resets = (v[A733_DISPLAY_HDMI_BUS] >> 16) & 7;
	state.ahb_gates = v[A733_DISPLAY_AHB] & (__BIT(4) | __BIT(5));
	state.mbus_gates = v[A733_DISPLAY_MBUS] & __BIT(11);
	for (u_int n = 0; n < __arraycount(muxes); n++) {
		const uint32_t value = v[muxes[n].reg];
		const u_int selector = (value >> 24) & 7;

		state.selector[n] = selector;
		state.divisor[n] = n == A733_DISPLAY_CONSUMER_HDMI_SFR ?
		    1 : (value & 31) + 1;
		if ((value & __BIT(31)) != 0)
			state.gates_enabled |= __BIT(n);
		if (selector > muxes[n].last)
			state.unknown_selectors |= __BIT(n);
		if (selector == muxes[n].de3x)
			state.de3x_selected |= __BIT(n);
		if (selector == muxes[n].de4x)
			state.de4x_selected |= __BIT(n);
		if ((n == A733_DISPLAY_CONSUMER_NSI ||
		    n == A733_DISPLAY_CONSUMER_DRAM0) &&
		    (value & __BIT(27)) != 0)
			state.update_pending |= __BIT(n);
	}
	state.hdmi_tv_divisor_n = ((v[A733_DISPLAY_HDMI_TV] >> 8) & 31) + 1;
	if ((v[A733_DISPLAY_NSI] & __BIT(28)) != 0)
		state.dynamic_mode |= __BIT(A733_DISPLAY_CONSUMER_NSI);
	*out = state;
	return 0;
}
