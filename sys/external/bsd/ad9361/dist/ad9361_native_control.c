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

/* Origin: EmberBSD staged RX integration of ADI no-OS
 * ce7107f957d19858f2dffc85bf3d7f2c70367000. */

#include <sys/param.h>
#include <sys/systm.h>
#include "ad9361.h"
#include "ad9361_native.h"
#include "ad9361_rx_tune.h"
#include "no_os_alloc.h"

static int ad9361_native_validate(const struct adi_axi_rx_config *config)
{
	unsigned int ch;

	if (config == NULL || config->frequency_hz < 70000000 ||
	    config->frequency_hz > 6000000000ULL ||
	    config->sample_rate_hz == 0 || config->sample_rate_hz > 61440000 ||
	    config->bandwidth_hz < 200000 || config->bandwidth_hz > 56000000 ||
	    config->rf_port > 8 ||
	    (config->rx_mask != 1 && config->rx_mask != 3) ||
	    config->channel_mask == 0 ||
	    (config->channel_mask & ~(config->rx_mask == 1 ? 3U : 15U)) != 0)
		return EINVAL;
	for (ch = 0; ch < (config->rx_mask == 1 ? 1U : 2U); ch++) {
		if (config->gain_mode[ch] != ADI_AXI_GAIN_MANUAL &&
		    config->gain_mode[ch] != ADI_AXI_GAIN_SLOW &&
		    config->gain_mode[ch] != ADI_AXI_GAIN_FAST)
			return EINVAL;
		if (config->gain_mode[ch] == ADI_AXI_GAIN_MANUAL &&
		    (config->gain_mdB[ch] % 1000) != 0)
			return EINVAL;
	}
	return 0;
}

static int ad9361_native_result(struct ad9361_native_bus *bus, int ret)
{

	if (bus->first_error != 0)
		return bus->first_error;
	return ret < 0 ? -ret : ret;
}

static int ad9361_native_apply(struct ad9361_native_bus *bus,
			       struct ad9361_rf_phy *phy,
			       const struct adi_axi_rx_config *config)
{
	static const uint8_t modes[] = {RF_GAIN_MGC, RF_GAIN_SLOWATTACK_AGC,
					RF_GAIN_FASTATTACK_AGC};
	uint32_t sample, quantum;
	uint64_t frequency;
	int32_t gain;
	unsigned int ch;
	int error, value;

#define CHECK(call)                                                            \
	do {                                                                   \
		error = ad9361_native_result(bus, (call));                     \
		if (error != 0)                                                \
			return error;                                          \
	} while (0)
	CHECK(ad9361_rx_only_inhibit(phy));
	CHECK(ad9361_set_rx_rf_port_input(phy, config->rf_port));
	CHECK(ad9361_set_rx_lo_freq(phy, config->frequency_hz));
	CHECK(ad9361_set_rx_sampling_freq(phy, config->sample_rate_hz));
	CHECK(ad9361_set_rx_rf_bandwidth(phy, config->bandwidth_hz));
	for (ch = 0; ch < (config->rx_mask == 1 ? 1U : 2U); ch++) {
		CHECK(ad9361_set_rx_gain_control_mode(
		    phy, ch, modes[config->gain_mode[ch]]));
		if (config->gain_mode[ch] == ADI_AXI_GAIN_MANUAL) {
			CHECK(ad9361_set_rx_rf_gain(
			    phy, ch, config->gain_mdB[ch] / 1000));
			CHECK(ad9361_get_rx_rf_gain(phy, ch, &gain));
			if (gain != config->gain_mdB[ch] / 1000)
				return EIO;
		}
	}
	CHECK(ad9361_get_rx_sampling_freq(phy, &sample));
	if (sample != config->sample_rate_hz)
		return EIO;
	/*
	 * RFPLL divider readback is quantized. The driver retains the actual
	 * value; this bound catches incorrect programming, not sub-Hz rounding.
	 */
	CHECK(ad9361_get_rx_lo_freq(phy, &frequency));
	quantum =
	    2 +
	    howmany(phy->clks[phy->ref_clk_scale[RX_RFPLL_INT]->parent_source]
			->rate,
		    RFPLL_MODULUS);
	if (frequency > config->frequency_hz + quantum ||
	    frequency + quantum < config->frequency_hz)
		return EIO;
	value = ad9361_spi_read(phy->spi, REG_RX_CP_OVERRANGE_VCO_LOCK);
	if (value < 0)
		return -value;
	if ((value & VCO_LOCK) == 0)
		return EIO;
	value = ad9361_spi_read(phy->spi, REG_CH_1_OVERFLOW);
	if (value < 0)
		return -value;
	if ((value & BBPLL_LOCK) == 0)
		return EIO;
	CHECK(ad9361_rx_only_verify(phy));
#undef CHECK
	return 0;
}

int ad9361_native_bootstrap(struct ad9361_native_bus *bus,
			    const AD9361_InitParam *settings,
			    const struct adi_axi_rx_config *config,
			    struct ad9361_rf_phy **result)
{
	AD9361_InitParam *params;
	struct ad9361_rf_phy *phy = NULL;
	int error;

	if (bus == NULL || settings == NULL || result == NULL)
		return EINVAL;
	*result = NULL;
	if (bus->tx_enabled)
		return EPERM;
	error = ad9361_native_validate(config);
	if (error != 0)
		return error;
	if (bus->first_error != 0)
		return bus->first_error;
	if (!settings->rx_only || settings->dev_sel != ID_AD9361 ||
	    (!settings->two_rx_two_tx_mode_enable &&
	    settings->one_rx_one_tx_mode_use_rx_num != 1) ||
	    !settings->frequency_division_duplex_mode_enable ||
	    settings->frequency_division_duplex_independent_mode_enable ||
	    settings->external_rx_lo_enable ||
	    settings->external_tx_lo_enable ||
	    settings->ensm_enable_txnrx_control_enable ||
	    settings->reference_clk_rate == 0)
		return EOPNOTSUPP;
	if (config->rx_mask !=
	    (settings->two_rx_two_tx_mode_enable ? 3U : 1U))
		return EINVAL;
	if (settings->aux_adc_rate == 0 || settings->rssi_duration == 0)
		return EINVAL;
	params = no_os_malloc(sizeof(*params));
	if (params == NULL)
		return ENOMEM;
	*params = *settings;
	params->rx_clock_bootstrap = true;
	params->rx_tune_io = NULL;
	params->spi_param.extra = bus;
	params->gpio_resetb.number = 0;
	params->gpio_resetb.extra = bus;
	params->gpio_sync.number = -1;
	params->gpio_cal_sw1.number = -1;
	params->gpio_cal_sw2.number = -1;
	params->tx_attenuation_mdB = 89750;
	params->rx_synthesizer_frequency_hz = config->frequency_hz;
	params->rx_rf_port_input_select = config->rf_port;
	params->rf_rx_bandwidth_hz = config->bandwidth_hz;
	error = ad9361_native_result(bus, ad9361_init(&phy, params));
	no_os_free(params);
	if (error == 0)
		error = ad9361_native_apply(bus, phy, config);
	if (error != 0) {
		(void)ad9361_native_bus_abort(bus);
		if (phy != NULL)
			ad9361_remove(phy);
		if (bus->first_error == 0)
			bus->first_error = error;
		return error;
	}
	/* Clock setup only: no ADC register access or capture-ready state. */
	*result = phy;
	return 0;
}

int ad9361_native_configure(struct ad9361_native_bus *bus,
			    struct ad9361_rf_phy *phy,
			    const struct adi_axi_rx_config *config,
			    const struct adi_axi_adc_io *io)
{
	struct ad9361_rx_tune_io tune;
	int error;

	if (bus == NULL || phy == NULL || io == NULL || io->reset == NULL ||
	    io->interface == NULL || io->channels == NULL ||
	    io->pn_select == NULL || io->pn_status == NULL)
		return EINVAL;
	if (bus->tx_enabled || !phy->pdata->rx_only)
		return EPERM;
	error = ad9361_native_validate(config);
	if (error != 0)
		return error;
	if (config->rx_mask != (phy->pdata->rx2tx2 ? 3U : 1U) ||
	    (!phy->pdata->rx2tx2 &&
	    phy->pdata->rx1tx1_mode_use_rx_num != 1))
		return EINVAL;
	tune.cookie = io->cookie;
	tune.reset = io->reset;
	tune.interface = io->interface;
	tune.channels = io->channels;
	tune.pn_select = io->pn_select;
	tune.pn_status = io->pn_status;
	error = ad9361_native_apply(bus, phy, config);
	if (error == 0)
		error =
		    ad9361_native_result(bus, ad9361_rx_only_tune(phy, &tune));
	if (error == 0)
		error = io->channels(io->cookie, config->channel_mask, 0x250);
	if (error == 0)
		error = ad9361_native_result(bus, ad9361_rx_only_verify(phy));
	if (error != 0) {
		(void)io->channels(io->cookie, 0, 0);
		(void)ad9361_native_bus_abort(bus);
		if (bus->first_error == 0)
			bus->first_error = error;
	}
	return error;
}

int
ad9361_native_read_rx_status(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, struct adi_axi_rx_status *status)
{
	struct rf_rssi rssi;
	int32_t gain;
	unsigned int ch;
	int error;

	if (bus == NULL || phy == NULL || status == NULL)
		return EINVAL;
	if (bus->tx_enabled || !phy->pdata->rx_only)
		return EPERM;
	memset(status, 0, sizeof(*status));
	for (ch = 0; ch < (phy->pdata->rx2tx2 ? 2U : 1U); ch++) {
		memset(&rssi, 0, sizeof(rssi));
		error = ad9361_native_result(bus,
		    ad9361_get_rx_rssi(phy, ch, &rssi));
		if (error != 0)
			return error;
		if (rssi.multiplier <= 0 || rssi.symbol > 1000000)
			return EIO;
		error = ad9361_native_result(bus,
		    ad9361_get_rx_rf_gain(phy, ch, &gain));
		if (error != 0)
			return error;
		status->rssi_mdB[ch] = -(int32_t)
		    ((uint64_t)rssi.symbol * 1000 / rssi.multiplier);
		status->gain_mdB[ch] = gain * 1000;
		status->valid_mask |= 1U << ch;
	}
	return 0;
}

/* Change only RX RFPLL. Sampling clocks, filters and digital interface stay
 * configured; the lease provider must have stopped every capture consumer. */
int
ad9361_native_retune_rx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, uint64_t target)
{
	uint64_t actual;
	uint32_t quantum;
	int error, value;

	if (bus == NULL || phy == NULL || target < 70000000 || target > 6000000000ULL)
		return EINVAL;
	if (bus->tx_enabled || !phy->pdata->rx_only)
		return EPERM;
	error = ad9361_native_result(bus, ad9361_rx_only_verify(phy));
	if (error == 0)
		error = ad9361_native_result(bus, ad9361_set_rx_lo_freq(phy, target));
	if (error == 0)
		error = ad9361_native_result(bus, ad9361_get_rx_lo_freq(phy, &actual));
	if (error == 0) {
		quantum = 2 + howmany(phy->clks[phy->ref_clk_scale[RX_RFPLL_INT]->parent_source]->rate, RFPLL_MODULUS);
		if (actual > target + quantum || actual + quantum < target)
			error = EIO;
	}
	if (error == 0) {
		value = ad9361_spi_read(phy->spi, REG_RX_CP_OVERRANGE_VCO_LOCK);
		error = value < 0 ? -value : ((value & VCO_LOCK) ? 0 : EIO);
	}
	if (error == 0)
		error = ad9361_native_result(bus, ad9361_rx_only_verify(phy));
	if (error != 0) {
		(void)ad9361_native_bus_abort(bus);
		if (bus->first_error == 0)
			bus->first_error = error;
	}
	return error;
}

/* TX policy is separate from the receive converter lease and transport. */
struct ad9361_native_fastlock {
	struct ad9361_rf_phy *phy;
	size_t count;
	unsigned int active_bank, staged_index;
	bool staged;
	struct {
		uint64_t frequency;
		uint8_t words[16];
	} profile[AD9361_NATIVE_TX_FASTLOCK_MAX];
};

static void
ad9361_native_fastlock_clear(struct ad9361_native_bus *bus)
{

	no_os_free(bus->tx_fastlock);
	bus->tx_fastlock = NULL;
}

static int
ad9361_native_tx_check(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy)
{

	if (bus == NULL || phy == NULL)
		return EINVAL;
	if (!bus->tx_enabled || phy->pdata->rx_only || phy->spi != &bus->spi)
		return EPERM;
	return bus->first_error;
}

static int
ad9361_native_tx_fail(struct ad9361_native_bus *bus, int error)
{

	if (error != 0) {
		ad9361_native_fastlock_clear(bus);
		if (bus->first_error == 0)
			bus->first_error = error > 0 ? error : -error;
		(void)ad9361_native_bus_abort(bus);
		return bus->first_error;
	}
	return 0;
}

/* Recalculate the entire TX clock chain from hardware, not cached parents. */
static int
ad9361_native_tx_clock(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy,
    uint32_t *sample)
{
	static const unsigned int clocks[] = {
	    ADC_CLK, DAC_CLK, T2_CLK, T1_CLK, CLKTF_CLK, TX_SAMPL_CLK
	};
	uint32_t rate;
	unsigned int i;

	rate = ad9361_clk_factor_recalc_rate(phy->ref_clk_scale[BB_REFCLK],
	    phy->clk_refin->rate);
	if (rate == 0 || bus->first_error != 0)
		return bus->first_error != 0 ? bus->first_error : EIO;
	rate = ad9361_bbpll_recalc_rate(phy->ref_clk_scale[BBPLL_CLK], rate);
	for (i = 0; i < __arraycount(clocks); i++) {
		if (rate == 0 || bus->first_error != 0)
			return bus->first_error != 0 ? bus->first_error : EIO;
		rate = ad9361_clk_factor_recalc_rate(phy->ref_clk_scale[clocks[i]],
		    rate);
	}
	*sample = rate;
	return ad9361_native_result(bus, rate != 0 ? 0 : EIO);
}

int
ad9361_native_read_tx_status(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, struct ad9361_native_tx_status *status)
{
	uint32_t ref;
	unsigned int ch;
	int error, value;

	if (status == NULL)
		return EINVAL;
	memset(status, 0, sizeof(*status));
	error = ad9361_native_tx_check(bus, phy);
	if (error != 0)
		return error;
#define TX_READ(reg, field) do { \
	value = ad9361_spi_read(phy->spi, (reg)); \
	if (value < 0) \
		return ad9361_native_tx_fail(bus, -value); \
	status->field = value; \
} while (0)
	/* RFPLL readback uses a freshly read reference divider. */
	ref = ad9361_clk_factor_recalc_rate(phy->ref_clk_scale[TX_REFCLK],
	    phy->clk_refin->rate);
	if (ref == 0 || bus->first_error != 0)
		return ad9361_native_tx_fail(bus,
		    bus->first_error != 0 ? bus->first_error : EIO);
	status->frequency_hz = ad9361_from_clk(ad9361_rfpll_int_recalc_rate(
	    phy->ref_clk_scale[TX_RFPLL_INT], ref));
	error = ad9361_native_tx_clock(bus, phy, &status->sample_rate_hz);
	if (error != 0)
		return ad9361_native_tx_fail(bus, error);
	for (ch = 0; ch < 2; ch++) {
		value = ad9361_get_tx_atten(phy, ch + 1);
		if (value < 0)
			return ad9361_native_tx_fail(bus, -value);
		status->attenuation_mdB[ch] = value;
	}
	TX_READ(REG_STATE, ensm_state);
	status->ensm_state &= ENSM_STATE(~0);
	TX_READ(REG_TX_ENABLE_FILTER_CTRL, tx_enable);
	TX_READ(REG_RX_CLOCK_DATA_DELAY, rx_delay);
	TX_READ(REG_TX_CLOCK_DATA_DELAY, tx_delay);
	value = ad9361_spi_read(phy->spi, REG_TX_CP_OVERRANGE_VCO_LOCK);
	if (value < 0)
		return ad9361_native_tx_fail(bus, -value);
	status->pll_locked = (value & VCO_LOCK) != 0;
	value = ad9361_spi_read(phy->spi, REG_CH_1_OVERFLOW);
	if (value < 0)
		return ad9361_native_tx_fail(bus, -value);
	status->bbpll_locked = (value & BBPLL_LOCK) != 0;
	status->quadrature_calibrated = true;
	for (ch = 0; ch < 2; ch++) {
		value = ad9361_spi_read(phy->spi, ch == 0 ?
		    REG_QUAD_CAL_STATUS_TX1 : REG_QUAD_CAL_STATUS_TX2);
		if (value < 0)
			return ad9361_native_tx_fail(bus, -value);
		if ((value & (TX1_LO_CONV | TX1_SSB_CONV)) !=
		    (TX1_LO_CONV | TX1_SSB_CONV))
			status->quadrature_calibrated = false;
	}
	/* Fixed board delay taps have not passed a digital eye search. */
	status->digital_tuning_valid = false;
#undef TX_READ
	if (status->attenuation_mdB[1] != AD9361_NATIVE_TX_MAX_ATTENUATION ||
	    status->attenuation_mdB[0] > AD9361_NATIVE_TX_MAX_ATTENUATION ||
	    !status->pll_locked || !status->bbpll_locked ||
	    !status->quadrature_calibrated)
		return ad9361_native_tx_fail(bus, EIO);
	return 0;
}

int
ad9361_native_set_tx_attenuation(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, uint32_t attenuation)
{
	int error, actual;

	if (attenuation > AD9361_NATIVE_TX_MAX_ATTENUATION || attenuation % 250)
		return EINVAL;
	error = ad9361_native_tx_check(bus, phy);
	if (error != 0)
		return error;
	error = ad9361_native_result(bus,
	    ad9361_set_tx_atten(phy, attenuation, true, false, true));
	if (error == 0) {
		actual = ad9361_get_tx_atten(phy, 1);
		error = actual < 0 ? -actual :
		    ((uint32_t)actual == attenuation ? 0 : EIO);
	}
	if (error == 0) {
		actual = ad9361_get_tx_atten(phy, 2);
		error = actual < 0 ? -actual :
		    (actual == AD9361_NATIVE_TX_MAX_ATTENUATION ? 0 : EIO);
	}
	return ad9361_native_tx_fail(bus, error);
}

int
ad9361_native_mute_tx(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy)
{

	return ad9361_native_set_tx_attenuation(bus, phy,
	    AD9361_NATIVE_TX_MAX_ATTENUATION);
}

int
ad9361_native_retune_tx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, uint64_t target)
{
	struct ad9361_native_tx_status status;
	uint32_t quantum;
	int error;

	if (target < 70000000 || target > 6000000000ULL)
		return EINVAL;
	error = ad9361_native_tx_check(bus, phy);
	if (error != 0)
		return error;
	/* The lease owner must keep DAC input zero across every retune. */
	ad9361_native_fastlock_clear(bus);
	if (phy->fastlock.current_profile[1] != 0) {
		/* A recall bypasses the ordinary clock cache and its equality guard. */
		phy->clks[TX_RFPLL]->rate = 0;
		phy->rfpll_requested_valid[1] = false;
	}
	error = ad9361_native_result(bus, ad9361_set_tx_lo_freq(phy, target));
	if (error == 0)
		error = ad9361_native_read_tx_status(bus, phy, &status);
	if (error == 0) {
		quantum = 2 + howmany(phy->clks[TX_REFCLK]->rate, RFPLL_MODULUS);
		if (status.frequency_hz > target + quantum ||
		    status.frequency_hz + quantum < target)
			error = EIO;
	}
	return ad9361_native_tx_fail(bus, error);
}

/* Check the selected bank and poll lock without recalibration or clock walks.
 * The delay budget is 100 us. SPI controller latency is additional.
 */
static int
ad9361_native_fastlock_selected(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, unsigned int bank, bool wait_lock)
{
	unsigned int i;
	int value;

	value = ad9361_spi_read(phy->spi, REG_TX_FAST_LOCK_SETUP);
	if (bus->first_error != 0 || value < 0)
		return ad9361_native_result(bus, value);
	if (value != (RX_FAST_LOCK_PROFILE(bank) | RX_FAST_LOCK_MODE_ENABLE) ||
	    phy->fastlock.current_profile[1] != bank + 1)
		return EIO;
	for (i = 0; i <= 20; i++) {
		value = ad9361_spi_read(phy->spi, REG_TX_CP_OVERRANGE_VCO_LOCK);
		if (bus->first_error != 0 || value < 0)
			return ad9361_native_result(bus, value);
		if ((value & VCO_LOCK) != 0)
			return 0;
		if (!wait_lock)
			return EIO;
		if (i != 20)
			delay(5);
	}
	return ETIMEDOUT;
}

/* Verify that the saved profile contains the RFPLL words just calibrated. */
static int
ad9361_native_fastlock_frequency(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, const uint8_t *words)
{
	uint8_t live[5];
	unsigned int i;
	int error, divider;

	error = ad9361_native_result(bus,
	    ad9361_spi_readm(phy->spi, REG_TX_FRACT_BYTE_2, live, sizeof(live)));
	if (error != 0)
		return error;
	for (i = 0; i < sizeof(live); i++)
		if (words[i] != live[sizeof(live) - 1 - i])
			return EIO;
	divider = ad9361_spi_read(phy->spi, REG_RFPLL_DIVIDERS);
	if (bus->first_error != 0 || divider < 0)
		return ad9361_native_result(bus, divider);
	return (words[12] & 0x0f) == ((divider >> 4) & 0x0f) ? 0 : EIO;
}

int
ad9361_native_fastlock_prepare_tx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, const uint64_t *frequencies, size_t count)
{
	struct ad9361_native_fastlock *cache;
	struct ad9361_native_tx_status status;
	uint64_t distance;
	size_t i;
	int error;

	error = ad9361_native_tx_check(bus, phy);
	if (error != 0)
		return error;
	if (frequencies == NULL || count == 0 ||
	    count > AD9361_NATIVE_TX_FASTLOCK_MAX)
		return EINVAL;
	for (i = 0; i < count; i++) {
		if (frequencies[i] < 70000000 || frequencies[i] > 6000000000ULL)
			return EINVAL;
		distance = frequencies[i] > frequencies[0] ?
		    frequencies[i] - frequencies[0] : frequencies[0] - frequencies[i];
		if (distance > phy->cal_threshold_freq)
			return EOPNOTSUPP;
	}
	error = ad9361_native_read_tx_status(bus, phy, &status);
	if (error != 0)
		return error;
	if (status.attenuation_mdB[0] != AD9361_NATIVE_TX_MAX_ATTENUATION)
		return EBUSY;
	cache = no_os_calloc(1, sizeof(*cache));
	if (cache == NULL)
		return ENOMEM;
	cache->phy = phy;
	cache->count = count;
	for (i = 0; i < count; i++)
		cache->profile[i].frequency = frequencies[i];
	for (i = 0; i < count; i++) {
		error = ad9361_native_retune_tx(bus, phy, cache->profile[i].frequency);
		if (error == 0 && i == 0) {
			error = ad9361_native_result(bus,
			    ad9361_do_calib_run(phy, TX_QUAD_CAL, -1));
			if (error == 0)
				phy->last_tx_quad_cal_freq = cache->profile[0].frequency;
		}
		if (error == 0)
			error = ad9361_native_result(bus,
			    ad9361_tx_fastlock_store(phy, 0));
		if (error == 0)
			error = ad9361_native_result(bus,
			    ad9361_tx_fastlock_save(phy, 0, cache->profile[i].words));
		if (error == 0)
			error = ad9361_native_fastlock_frequency(bus, phy,
			    cache->profile[i].words);
		if (error != 0)
			goto fail;
	}
	error = ad9361_native_result(bus,
	    ad9361_tx_fastlock_load(phy, 0, cache->profile[0].words));
	if (error == 0)
		error = ad9361_native_result(bus, ad9361_tx_fastlock_recall(phy, 0));
	if (error == 0)
		error = ad9361_native_fastlock_selected(bus, phy, 0, true);
	if (error == 0)
		error = ad9361_native_read_tx_status(bus, phy, &status);
	if (error == 0) {
		uint32_t quantum = 2 + howmany(phy->clks[TX_REFCLK]->rate,
		    RFPLL_MODULUS);
		if (status.frequency_hz > cache->profile[0].frequency + quantum ||
		    status.frequency_hz + quantum < cache->profile[0].frequency ||
		    status.attenuation_mdB[0] != AD9361_NATIVE_TX_MAX_ATTENUATION)
			error = EIO;
	}
	if (error != 0)
		goto fail;
	bus->tx_fastlock = cache;
	return 0;
fail:
	no_os_free(cache);
	return ad9361_native_tx_fail(bus, error);
}

int
ad9361_native_fastlock_stage_tx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, unsigned int index)
{
	struct ad9361_native_fastlock *cache;
	uint8_t words[16];
	unsigned int bank;
	int error;

	error = ad9361_native_tx_check(bus, phy);
	if (error != 0)
		return error;
	cache = bus->tx_fastlock;
	if (cache == NULL || cache->phy != phy || index >= cache->count)
		return EINVAL;
	error = ad9361_native_fastlock_selected(bus, phy, cache->active_bank, false);
	if (error != 0)
		return ad9361_native_tx_fail(bus, error);
	bank = cache->active_bank ^ 1U;
	cache->staged = false;
	error = ad9361_native_result(bus,
	    ad9361_tx_fastlock_load(phy, bank, cache->profile[index].words));
	if (error == 0)
		error = ad9361_native_result(bus,
		    ad9361_tx_fastlock_save(phy, bank, words));
	if (error == 0 && memcmp(words, cache->profile[index].words,
	    sizeof(words)) != 0)
		error = EIO;
	if (error == 0)
		error = ad9361_native_fastlock_selected(bus, phy, cache->active_bank, false);
	if (error != 0)
		return ad9361_native_tx_fail(bus, error);
	cache->staged_index = index;
	cache->staged = true;
	return 0;
}

int
ad9361_native_fastlock_recall_tx(struct ad9361_native_bus *bus,
    struct ad9361_rf_phy *phy, unsigned int index)
{
	struct ad9361_native_fastlock *cache;
	unsigned int bank;
	int error;

	error = ad9361_native_tx_check(bus, phy);
	if (error != 0)
		return error;
	cache = bus->tx_fastlock;
	if (cache == NULL || cache->phy != phy || !cache->staged ||
	    index != cache->staged_index)
		return EINVAL;
	bank = cache->active_bank ^ 1U;
	cache->staged = false;
	/* The consumer must verify zero DAC samples before this call. */
	error = ad9361_native_fastlock_selected(bus, phy, cache->active_bank, false);
	if (error == 0)
		error = ad9361_native_result(bus, ad9361_tx_fastlock_recall(phy, bank));
	if (error == 0)
		error = ad9361_native_fastlock_selected(bus, phy, bank, true);
	if (error != 0)
		return ad9361_native_tx_fail(bus, error);
	cache->active_bank = bank;
	return 0;
}

int
ad9361_native_bootstrap_tx(struct ad9361_native_bus *bus,
    const AD9361_InitParam *settings, uint64_t frequency,
    struct ad9361_rf_phy **result)
{
	static const uint32_t clocks[] = {
	    832000000, 104000000, 52000000, 26000000, 13000000, 13000000
	};
	AD9361_InitParam *params;
	struct ad9361_rf_phy *phy = NULL;
	struct ad9361_native_tx_status status;
	int error;

	if (bus == NULL || settings == NULL || result == NULL ||
	    frequency < 70000000 || frequency > 6000000000ULL)
		return EINVAL;
	*result = NULL;
	if (!bus->tx_enabled || settings->rx_only ||
	    !settings->native_tx_fixed_delays || settings->dev_sel != ID_AD9361 ||
	    !settings->lvds_mode_enable || settings->single_data_rate_enable ||
	    settings->single_port_mode_enable || settings->half_duplex_mode_enable ||
	    settings->fdd_rx_rate_2tx_enable || settings->rx_data_clock_delay > 15 ||
	    settings->rx_data_delay > 15 || settings->tx_fb_clock_delay > 15 ||
	    settings->tx_data_delay > 15 ||
	    !settings->two_rx_two_tx_mode_enable ||
	    !settings->frequency_division_duplex_mode_enable ||
	    settings->frequency_division_duplex_independent_mode_enable ||
	    settings->external_rx_lo_enable || settings->external_tx_lo_enable ||
	    settings->ensm_enable_txnrx_control_enable ||
	    settings->tx_fastlock_pincontrol_enable ||
	    settings->rx_fastlock_pincontrol_enable ||
	    settings->reference_clk_rate == 0)
		return EOPNOTSUPP;
	if (settings->aux_adc_rate == 0 || settings->rssi_duration == 0)
		return EINVAL;
	if (bus->first_error != 0)
		return bus->first_error;
	params = no_os_malloc(sizeof(*params));
	if (params == NULL)
		return ENOMEM;
	*params = *settings;
	params->rx_clock_bootstrap = false;
	params->rx_tune_io = NULL;
	params->spi_param.extra = bus;
	params->gpio_resetb.number = 0;
	params->gpio_resetb.extra = bus;
	params->gpio_sync.number = -1;
	params->gpio_cal_sw1.number = -1;
	params->gpio_cal_sw2.number = -1;
	params->tx_attenuation_mdB = AD9361_NATIVE_TX_MAX_ATTENUATION;
	params->rx_synthesizer_frequency_hz = frequency;
	params->tx_synthesizer_frequency_hz = frequency;
	params->rf_rx_bandwidth_hz = AD9361_NATIVE_TX_BANDWIDTH;
	params->rf_tx_bandwidth_hz = AD9361_NATIVE_TX_BANDWIDTH;
	memcpy(params->rx_path_clock_frequencies, clocks, sizeof(clocks));
	memcpy(params->tx_path_clock_frequencies, clocks, sizeof(clocks));
	error = ad9361_native_result(bus, ad9361_init(&phy, params));
	no_os_free(params);
	if (error == 0)
		error = ad9361_native_mute_tx(bus, phy);
	if (error == 0)
		error = ad9361_native_retune_tx(bus, phy, frequency);
	if (error == 0)
		error = ad9361_native_read_tx_status(bus, phy, &status);
	if (error == 0 && (status.sample_rate_hz != AD9361_NATIVE_TX_RATE ||
	    status.ensm_state != ENSM_STATE_FDD ||
	    (status.tx_enable & 0xc0) != 0xc0 ||
	    status.rx_delay != ((settings->rx_data_clock_delay << 4) |
	    settings->rx_data_delay) ||
	    status.tx_delay != ((settings->tx_fb_clock_delay << 4) |
	    settings->tx_data_delay)))
		error = EIO;
	if (error != 0) {
		error = ad9361_native_tx_fail(bus, error);
		if (phy != NULL)
			ad9361_remove(phy);
		return error;
	}
	*result = phy;
	return 0;
}

int
ad9361_native_destroy_tx(struct ad9361_native_bus *bus, struct ad9361_rf_phy *phy)
{
	int error;

	if (bus == NULL || !bus->tx_enabled ||
	    (phy != NULL && (phy->pdata->rx_only || phy->spi != &bus->spi)))
		return EINVAL;
	/* Cleanup is mandatory even when the SPI transport has failed. */
	ad9361_native_fastlock_clear(bus);
	error = ad9361_native_bus_abort(bus);
	if (phy != NULL)
		ad9361_remove(phy);
	return error < 0 ? -error : error;
}
