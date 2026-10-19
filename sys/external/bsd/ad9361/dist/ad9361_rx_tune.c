#include <sys/param.h>
#include <sys/systm.h>
/*
 * Copyright (c) 2026 EmberBSD contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Origin: Experimental AD9361 RX PN eye scan using the ADI no-OS API. */
#include "ad9361.h"
#include "ad9361_util.h"
#include "ad9361_rx_tune.h"
#include "no_os_delay.h"
#include <sys/systm.h>

static int32_t rx_state(struct ad9361_rf_phy *phy, uint8_t state)
{
	int32_t value;
	unsigned int retry;

	ad9361_ensm_force_state(phy, state);
	for (retry = 0; retry < 20; retry++) {
		value = ad9361_spi_read(phy->spi, REG_STATE);
		if (value < 0)
			return value;
		if ((value & 0xf) == state)
			return ad9361_rx_only_verify(phy);
		no_os_mdelay(1);
	}
	return -ETIMEDOUT;
}

static int32_t rx_delay(struct ad9361_rf_phy *phy, uint8_t value)
{
	int32_t ret;

	/* Latch every candidate in ALERT, including data-only changes. */
	ret = rx_state(phy, ENSM_STATE_ALERT);
	if (ret < 0)
		return ret;
	ret = ad9361_spi_write(phy->spi, REG_RX_CLOCK_DATA_DELAY, value);
	if (ret < 0)
		return ret;
	return rx_state(phy, ENSM_STATE_FDD);
}

static int32_t rx_pn_check(const struct ad9361_rx_tune_io *io,
			   unsigned int channels)
{
	unsigned int channel;
	uint32_t status;
	int ret, bad = 0;

	for (channel = 0; channel < channels; channel++) {
		ret = io->pn_status(io->cookie, channel, true, &status);
		if (ret != 0 && ret != EIO)
			return -ret;
	}
	no_os_mdelay(4);
	for (channel = 0; channel < channels; channel++) {
		ret = io->pn_status(io->cookie, channel, false, &status);
		if (ret != 0 && ret != EIO)
			return -ret;
		if (ret != 0 || status != 0)
			bad = 1;
	}
	return bad;
}

int32_t ad9361_rx_only_tune(struct ad9361_rf_phy *phy,
			    const struct ad9361_rx_tune_io *io)
{
	unsigned int clk, data, channel, count, run, best = 0;
	unsigned int best_clk = 0, best_data = 0;
	uint8_t selected;
	int32_t ret, saved_bist, readback;

	if (!phy || !phy->pdata->rx_only || !io || !io->reset ||
	    !io->interface || !io->channels || !io->pn_select || !io->pn_status)
		return -EINVAL;
	if (!phy->pdata->fdd || phy->pdata->ensm_pin_ctrl)
		return -EOPNOTSUPP;
	ret = ad9361_rx_only_verify(phy);
	if (ret < 0)
		return ret;
	saved_bist = ad9361_spi_read(phy->spi, REG_BIST_CONFIG);
	if (saved_bist < 0)
		return saved_bist;
	if (saved_bist != 0 || phy->bist_loopback_mode != 0)
		return -EBUSY;
	count = phy->pdata->rx2tx2 ? 4 : 2;
	ret = io->interface(io->cookie, phy->pdata->rx2tx2,
			    (phy->pdata->port_ctrl.pp_conf[2] & LVDS_MODE) != 0,
			    phy->pdata->axi_half_dac_rate_en);
	if (ret != 0)
		return -ret;
	ret = io->reset(io->cookie);
	if (ret != 0)
		return -ret;
	/* Sign extension, format enable and unity IQ correction. */
	ret = io->channels(io->cookie, (1U << count) - 1, 0x250);
	if (ret != 0)
		return -ret;
	for (channel = 0; channel < count; channel++) {
		ret = io->pn_select(io->cookie, channel, 0); /* PN9 */
		if (ret != 0) {
			ret = -ret;
			goto fail;
		}
	}
	ret = ad9361_bist_prbs(phy, BIST_INJ_RX);
	if (ret < 0)
		goto fail;
	for (clk = 0; clk < 16; clk++) {
		run = 0;
		for (data = 0; data < 16; data++) {
			ret = rx_delay(phy, (clk << 4) | data);
			if (ret < 0)
				goto fail;
			ret = rx_pn_check(io, count);
			if (ret < 0)
				goto fail;
			run = ret == 0 ? run + 1 : 0;
			if (run > best) {
				best = run;
				best_clk = clk;
				best_data = data + 1 - run + run / 2;
			}
		}
	}
	if (best == 0) {
		ret = -EIO;
		goto fail;
	}
	selected = (best_clk << 4) | best_data;
	ret = rx_delay(phy, selected);
	if (ret < 0)
		goto fail;
	ret = rx_pn_check(io, count);
	if (ret != 0) {
		if (ret > 0)
			ret = -EIO;
		goto fail;
	}
	readback = ad9361_spi_read(phy->spi, REG_RX_CLOCK_DATA_DELAY);
	if (readback < 0 || readback != selected) {
		ret = readback < 0 ? readback : -EIO;
		goto fail;
	}
	ret = ad9361_bist_prbs(phy, BIST_DISABLE);
	if (ret < 0)
		goto fail;
	phy->pdata->port_ctrl.rx_clk_data_delay = selected;
	return ad9361_rx_only_verify(phy);
fail:
	(void)ad9361_bist_prbs(phy, BIST_DISABLE);
	(void)rx_state(phy, ENSM_STATE_ALERT);
	(void)io->channels(io->cookie, 0, 0);
	return ret;
}
