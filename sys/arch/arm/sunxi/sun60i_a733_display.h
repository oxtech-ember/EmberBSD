/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _SUN60I_A733_DISPLAY_H
#define _SUN60I_A733_DISPLAY_H

/* Keep the original snapshot indices stable when adding observations. */
enum sun60i_a733_display_clock_index {
	A733_DISPLAY_PLL_DE, A733_DISPLAY_AHB, A733_DISPLAY_MBUS,
	A733_DISPLAY_DE0, A733_DISPLAY_DE0_BUS, A733_DISPLAY_DE_SYS_RESET,
	A733_DISPLAY_TV0_BUS, A733_DISPLAY_TV1_BUS, A733_DISPLAY_HDMI_TV,
	A733_DISPLAY_HDMI_BUS, A733_DISPLAY_HDMI_SFR,
	A733_DISPLAY_DPSS_TOP1_BUS, A733_DISPLAY_VO1_RESET,
	A733_DISPLAY_DEPLL_GATE, A733_DISPLAY_DEPLL_STATUS,
	A733_DISPLAY_PLL_DE_PAT0, A733_DISPLAY_PLL_DE_PAT1,
	A733_DISPLAY_NSI, A733_DISPLAY_VE_ENC0, A733_DISPLAY_VE_DEC,
	A733_DISPLAY_NPU, A733_DISPLAY_DRAM0, A733_DISPLAY_CSI,
	A733_DISPLAY_NCLOCK_REGS
};
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

/* Bitmap order for the decoded mux observations. */
enum sun60i_a733_display_consumer {
	A733_DISPLAY_CONSUMER_DE0, A733_DISPLAY_CONSUMER_NSI,
	A733_DISPLAY_CONSUMER_VE_ENC0, A733_DISPLAY_CONSUMER_VE_DEC,
	A733_DISPLAY_CONSUMER_NPU, A733_DISPLAY_CONSUMER_DRAM0,
	A733_DISPLAY_CONSUMER_CSI, A733_DISPLAY_CONSUMER_HDMI_TV,
	A733_DISPLAY_CONSUMER_HDMI_SFR, A733_DISPLAY_NCONSUMERS
};

struct sun60i_a733_display_clock_facts {
	/* Raw documented fields only; no rate or readiness inference. */
	uint32_t pll_control;
	uint32_t pll_pattern0;
	uint32_t pll_pattern1;
	uint32_t pll_gate_control;
	uint32_t pll_gate_status;
	/* Indexed by clock index. HDMI reset bits are reported separately. */
	uint32_t bus_gates;
	uint32_t resets_deasserted;
	uint32_t hdmi_resets;
	uint32_t ahb_gates;
	uint32_t mbus_gates;
	/* Indexed by consumer. A selected source need not be active. */
	uint32_t de3x_selected;
	uint32_t de4x_selected;
	uint32_t gates_enabled;
	uint32_t unknown_selectors;
	uint32_t update_pending;
	uint32_t dynamic_mode;
	uint8_t selector[A733_DISPLAY_NCONSUMERS];
	uint8_t divisor[A733_DISPLAY_NCONSUMERS];
	uint8_t hdmi_tv_divisor_n;
};

/* Decode an unchanged snapshot. This function performs no I/O. */
int sun60i_a733_display_decode_clocks(const struct sun60i_a733_display_clocks *,
    struct sun60i_a733_display_clock_facts *);

/* These snapshots never authorize access to DE, TCON, HDMI or PHY MMIO. */
int sun60i_a733_display_read_clocks(sun60i_a733_display_read_fn, void *,
    uint64_t, struct sun60i_a733_display_clocks *);
int sun60i_a733_display_read_power(sun60i_a733_display_power_fn, void *,
    struct sun60i_a733_display_power *);

#endif
