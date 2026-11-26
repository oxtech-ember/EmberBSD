/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 The EmberBSD Project. */

#ifndef _DEV_IC_DWC_MMC_CLOCK_H_
#define _DEV_IC_DWC_MMC_CLOCK_H_

/* CLKDIV[7:0] divides by twice its value. Zero bypasses this divider. */
static inline int
dwc_mmc_clock_divider(uint32_t input_hz, unsigned int fixed_div,
    unsigned int card_khz)
{
	const uint64_t target = (uint64_t)card_khz * 1000;
	uint64_t bus, div;

	if (input_hz == 0 || card_khz == 0)
		return -1;
	if (fixed_div == 0)
		fixed_div = 1;
	bus = ((uint64_t)input_hz + fixed_div - 1) / fixed_div;
	if (bus <= target)
		return 0;
	div = (bus + 2 * target - 1) / (2 * target);
	return div <= 255 ? (int)div : -1;
}

#endif /* _DEV_IC_DWC_MMC_CLOCK_H_ */
