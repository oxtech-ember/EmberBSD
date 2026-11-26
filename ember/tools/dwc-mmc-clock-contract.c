/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 The EmberBSD Project. */

#include <assert.h>
#include <stdint.h>

#include <dev/ic/dwc_mmc_clock.h>

int
main(void)
{
	/* The CRU supplies twice the requested card clock on Rockchip. */
	assert(dwc_mmc_clock_divider(800000, 2, 400) == 0);
	assert(dwc_mmc_clock_divider(24000000, 2, 12000) == 0);
	assert(dwc_mmc_clock_divider(24000000, 2, 400) == 15);
	assert(dwc_mmc_clock_divider(100000000, 2, 25000) == 1);

	/* A generic fixed input with the bypass, a divider, and rounding. */
	assert(dwc_mmc_clock_divider(12000000, 0, 12000) == 0);
	assert(dwc_mmc_clock_divider(12000000, 1, 25000) == 0);
	assert(dwc_mmc_clock_divider(50000001, 1, 25000) == 2);
	assert(dwc_mmc_clock_divider(51000000, 1, 100) == 255);
	assert(dwc_mmc_clock_divider(51000001, 1, 100) == -1);
	assert(dwc_mmc_clock_divider(0, 1, 400) == -1);
	assert(dwc_mmc_clock_divider(UINT32_MAX, 1, 0) == -1);
	assert(dwc_mmc_clock_divider(UINT32_MAX, 1, UINT32_MAX) == 0);

	return 0;
}
