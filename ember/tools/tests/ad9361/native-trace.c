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
/* Origin: EmberBSD AD9361 register-sequence and transport-failure regression.
 */

#include "shim.h"
#include "ad9361.h"
#include "ad9361_native.h"
/* Compile both public interfaces together to reject symbol collisions. */
#include "ad9361var.h"
struct prop {
	const char *name;
	int size;
	const uint8_t *value;
};
static const uint8_t present[1] = {0};
static const struct prop props[] = {
    {"adi,rx-only", 0, present},
    {"adi,digital-interface-tune-fir-disable", 0, present},
    {"adi,2rx-2tx-mode-enable", 0, present},
    {"adi,frequency-division-duplex-mode-enable", 0, present},
    {"adi,lvds-mode-enable", 0, present}};
struct fake {
	uint8_t reg[1024];
	uint8_t fast[8][16];
	unsigned transfers, writes, resets, pn_reads, fail_at;
	unsigned fast_writes;
	bool bad_cal, bad_pll, tx, bad_tx_pll, corrupt_atten, fail_reset, bad_quad;
	bool corrupt_fast, corrupt_setup;
	bool lose_lock_on_recall, corrupt_recall;
} hw;
static bool invalid_aux_rate, tx_fdt, conflicting_fdt, one_rx_fdt;
static bool extended_rx_fdt;
static const char * const tx_delays[] = {
    "adi,rx-data-clock-delay", "adi,rx-data-delay",
    "adi,tx-fb-clock-delay", "adi,tx-data-delay"
};

int OF_getproplen(int node, const char *name)
{
	if (strcmp(name, "emberbsd,experimental-rx-extended-range") == 0)
		return extended_rx_fdt ? 0 : -1;
	if (one_rx_fdt && (strcmp(name, "adi,2rx-2tx-mode-enable") == 0 ||
	    strcmp(name, "adi,lvds-mode-enable") == 0))
		return -1;
	if (strcmp(name, "adi,rx-only") == 0)
		return !tx_fdt || conflicting_fdt ? 0 : -1;
	if (strcmp(name, "adi,tx-enable") == 0)
		return tx_fdt || conflicting_fdt ? 0 : -1;
	if (tx_fdt)
		for (unsigned i = 0; i < __arraycount(tx_delays); i++)
			if (strcmp(name, tx_delays[i]) == 0)
				return 4;
	if (invalid_aux_rate && strcmp(name, "adi,aux-adc-rate") == 0)
		return 4;
	(void)node;
	for (unsigned i = 0; i < __arraycount(props); i++)
		if (!strcmp(props[i].name, name))
			return props[i].size;
	return -1;
}
int OF_getprop(int node, const char *name, void *data, int size)
{
	if (tx_fdt)
		for (unsigned i = 0; i < __arraycount(tx_delays); i++)
			if (strcmp(name, tx_delays[i]) == 0) {
				assert(size == 4);
				memset(data, 0, 4);
				((uint8_t *)data)[3] = i == 1 ? 4 : i == 2 ? 7 : 0;
				return 4;
			}
	if (invalid_aux_rate && strcmp(name, "adi,aux-adc-rate") == 0) {
		assert(size == 4);
		memset(data, 0, 4);
		return 4;
	}
	(void)node;
	for (unsigned i = 0; i < __arraycount(props); i++)
		if (!strcmp(props[i].name, name)) {
			if (size < props[i].size)
				return -1;
			memcpy(data, props[i].value, props[i].size);
			return props[i].size;
		}
	return -1;
}
bool of_hasprop(int node, const char *name)
{
	return OF_getproplen(node, name) >= 0;
}
int of_compatible(int node, const char * const *names)
{
	const char *actual = one_rx_fdt ? "adi,ad9363a" : "adi,ad9361";
	(void)node;
	for (; *names != NULL; names++)
		if (strcmp(*names, actual) == 0)
			return 1;
	return 0;
}
static int reset(void *cookie, bool high)
{
	struct fake *f = cookie;
	f->resets++;
	if (f->fail_reset)
		return EIO;
	if (!high) {
		memset(f->reg, 0, sizeof(f->reg));
		memset(f->fast, 0, sizeof(f->fast));
		f->reg[REG_PRODUCT_ID] = 0x0a;
		f->reg[REG_TX_ENABLE_FILTER_CTRL] = 0x5f;
		f->reg[REG_RX_ENABLE_FILTER_CTRL] = 0x5f;
	}
	return 0;
}
static int spi(void *cookie, uint8_t *data, size_t n)
{
	struct fake *f = cookie;
	unsigned cmd = (data[0] << 8) | data[1], reg = cmd & 0x3ff,
		 count = ((cmd >> 12) & 7) + 1;
	f->transfers++;
	assert(n == count + 2);
	if (f->transfers == f->fail_at)
		return EIO;
	for (unsigned i = 0; i < count; i++) {
		unsigned r = reg - i;
		if (cmd & 0x8000) {
			int e = f->tx ? 0 : ad9361_native_check_write(r, data[i + 2]);
			if (e) {
				fprintf(stderr, "FORBIDDEN %#x=%#x\n", r,
					data[i + 2]);
				abort();
			}
			f->writes++;
			f->reg[r] = data[i + 2];
			if (r == REG_TX_FAST_LOCK_SETUP &&
			    (data[i + 2] & RX_FAST_LOCK_MODE_ENABLE) != 0) {
				if (f->lose_lock_on_recall)
					f->bad_tx_pll = true;
				if (f->corrupt_recall)
					f->reg[r] ^= RX_FAST_LOCK_PROFILE(1);
			}
			if (r == REG_TX_FAST_LOCK_PROGRAM_CTRL &&
			    (data[i + 2] & RX_FAST_LOCK_PROGRAM_WRITE) != 0) {
				unsigned addr = f->reg[REG_TX_FAST_LOCK_PROGRAM_ADDR];
				unsigned bank = (addr >> 4) & 7;
				unsigned setup = f->reg[REG_TX_FAST_LOCK_SETUP];
				/* No operation may overwrite the active hardware bank. */
				assert(!(setup & RX_FAST_LOCK_MODE_ENABLE) ||
				    bank != (setup >> 5));
				f->fast[bank][addr & 15] = f->reg[REG_TX_FAST_LOCK_PROGRAM_DATA];
				f->fast_writes++;
			}
			if (r == REG_ENSM_CONFIG_1)
				f->reg[REG_STATE] =
				    (data[i + 2] & FORCE_TX_ON)
					? ENSM_STATE_FDD
					: ((data[i + 2] & FORCE_ALERT_STATE)
					       ? ENSM_STATE_ALERT
					       : ENSM_STATE_SLEEP_WAIT);
		} else {
			uint8_t v = f->reg[r];
			if (r == REG_TX_FAST_LOCK_PROGRAM_READ) {
				unsigned addr = f->reg[REG_TX_FAST_LOCK_PROGRAM_ADDR];
				v = f->fast[(addr >> 4) & 7][addr & 15];
				if (f->corrupt_fast)
					v ^= 1;
			}
			if (r == REG_TX_FAST_LOCK_SETUP && f->corrupt_setup)
				v ^= RX_FAST_LOCK_PROFILE(1);
			if (r == REG_CALIBRATION_CTRL)
				v = 0;
			if (r == REG_RX_BBF_R2346)
				v = f->bad_cal ? 0 : 1;
			if (r == REG_RX_CAL_STATUS || r == REG_TX_CAL_STATUS)
				v |= CP_CAL_VALID;
			if (r == REG_CH_1_OVERFLOW) {
				if (!f->bad_pll)
					v |= BBPLL_LOCK;
				else
					v &= ~BBPLL_LOCK;
			}
			if (r == REG_RX_CP_OVERRANGE_VCO_LOCK ||
			    (r == REG_TX_CP_OVERRANGE_VCO_LOCK && !f->bad_tx_pll))
				v |= VCO_LOCK;
			if (!f->bad_quad && (r == REG_QUAD_CAL_STATUS_TX1 ||
			    r == REG_QUAD_CAL_STATUS_TX2))
				v |= TX1_LO_CONV | TX1_SSB_CONV;
			if (r == REG_TX1_ATTEN_0 && f->corrupt_atten)
				v ^= 1;
			data[i + 2] = v;
		}
	}
	return 0;
}
static int adc_reset(void *c)
{
	(void)c;
	return 0;
}
static int adc_interface(void *c, bool two, bool lvds, bool half)
{
	(void)c;
	(void)lvds;
	(void)half;
	assert(two == !one_rx_fdt);
	return 0;
}
static int adc_channels(void *c, uint32_t mask, uint32_t format)
{
	(void)c;
	(void)format;
	assert(!(mask & ~15U));
	return 0;
}
static int adc_pn_select(void *c, unsigned ch, unsigned pn)
{
	(void)c;
	assert(ch < 4);
	assert(pn == 0);
	return 0;
}
static int adc_pn_status(void *c, unsigned ch, bool clear, uint32_t *status)
{
	struct fake *f = c;
	(void)clear;
	assert(ch < 4);
	*status = 0;
	f->pn_reads++;
	return 0;
}
static struct ad9361_rf_phy *
fastlock_bootstrap(struct ad9361_native_bus *bus)
{
	AD9361_InitParam p;
	struct ad9361_rf_phy *phy = NULL;

	tx_fdt = true;
	assert(ad9361_native_params_from_fdt_tx(1, 40000000, &p) == 0);
	memset(&hw, 0, sizeof(hw));
	hw.tx = true;
	ad9361_native_bus_init_tx(bus, &hw, spi, reset);
	assert(ad9361_native_bootstrap_tx(bus, &p, 2400000000ULL, &phy) == 0);
	return phy;
}

static void
test_fastlock(void)
{
	const uint64_t frequencies[] = {2400000000ULL, 2440000000ULL};
	const uint64_t distant[] = {2400000000ULL, 2600000000ULL};
	uint64_t many[AD9361_NATIVE_TX_FASTLOCK_MAX];
	struct ad9361_native_bus bus;
	struct ad9361_rf_phy *phy;
	struct ad9361_native_tx_status status;
	uint8_t active[16];
	unsigned start, counts[4], before;
	int error;

	phy = fastlock_bootstrap(&bus);
	before = hw.transfers;
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, NULL, 2) == EINVAL);
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, frequencies, 0) == EINVAL);
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, frequencies, 129) == EINVAL);
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, distant, 2) == EOPNOTSUPP);
	assert(ad9361_native_fastlock_stage_tx(&bus, phy, 0) == EINVAL);
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 0) == EINVAL);
	bus.tx_enabled = false;
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, frequencies, 2) == EPERM);
	assert(ad9361_native_fastlock_stage_tx(&bus, phy, 0) == EPERM);
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 0) == EPERM);
	bus.tx_enabled = true;
	assert(hw.transfers == before);
	assert(ad9361_native_set_tx_attenuation(&bus, phy, 30000) == 0);
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, frequencies, 2) == EBUSY);
	assert(ad9361_native_mute_tx(&bus, phy) == 0);
	start = hw.transfers;
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, frequencies, 2) == 0);
	counts[0] = hw.transfers - start;
	assert(bus.tx_fastlock != NULL);
	assert(phy->fastlock.current_profile[1] == 1);
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.frequency_hz == frequencies[0]);
	assert(ad9361_native_set_tx_attenuation(&bus, phy, 30000) == 0);
	memcpy(active, hw.fast[0], sizeof(active));
	start = hw.transfers;
	assert(ad9361_native_fastlock_stage_tx(&bus, phy, 1) == 0);
	counts[1] = hw.transfers - start;
	assert(memcmp(active, hw.fast[0], sizeof(active)) == 0);
	assert(hw.reg[REG_TX_FAST_LOCK_SETUP] == RX_FAST_LOCK_MODE_ENABLE);
	before = hw.transfers;
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 0) == EINVAL);
	assert(ad9361_native_fastlock_stage_tx(&bus, phy, 2) == EINVAL);
	assert(hw.transfers == before);
	start = hw.transfers;
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 1) == 0);
	counts[2] = hw.transfers - start;
	/* Only the profile selection, ALC workaround and lock checks are needed. */
	assert(counts[2] <= 9);
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.frequency_hz == frequencies[1]);
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 1) == EINVAL);
	assert(ad9361_native_fastlock_stage_tx(&bus, phy, 1) == 0);
	start = hw.transfers;
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 1) == 0);
	counts[3] = hw.transfers - start;
	assert(counts[3] == 9);
	assert(ad9361_native_fastlock_stage_tx(&bus, phy, 0) == 0);
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 0) == 0);
	/* The ordinary setter must not skip a frequency retained in its cache. */
	assert(ad9361_native_retune_tx(&bus, phy, frequencies[1]) == 0);
	assert(bus.tx_fastlock == NULL && phy->fastlock.current_profile[1] == 0);
	assert(hw.reg[REG_TX_FAST_LOCK_SETUP] == 0);
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.frequency_hz == frequencies[1]);
	assert(ad9361_native_destroy_tx(&bus, phy) == 0);
	for (unsigned phase = 0; phase < 4; phase++) {
		for (unsigned fault = 1; fault <= counts[phase]; fault++) {
			phy = fastlock_bootstrap(&bus);
			if (phase > 0)
				assert(ad9361_native_fastlock_prepare_tx(&bus, phy,
				    frequencies, 2) == 0);
			if (phase > 1)
				assert(ad9361_native_fastlock_stage_tx(&bus, phy, 1) == 0);
			if (phase > 2) {
				assert(ad9361_native_fastlock_recall_tx(&bus, phy, 1) == 0);
				assert(ad9361_native_fastlock_stage_tx(&bus, phy, 1) == 0);
			}
			hw.fail_at = hw.transfers + fault;
			error = phase == 0 ? ad9361_native_fastlock_prepare_tx(&bus,
			    phy, frequencies, 2) : phase == 1 ?
			    ad9361_native_fastlock_stage_tx(&bus, phy, 1) :
			    ad9361_native_fastlock_recall_tx(&bus, phy, 1);
			assert(error == EIO && bus.first_error == EIO);
			assert(!bus.reset_released && bus.tx_fastlock == NULL);
			assert(hw.transfers == hw.fail_at);
			assert(ad9361_native_fastlock_stage_tx(&bus, phy, 0) == EIO);
			assert(hw.transfers == hw.fail_at);
			assert(ad9361_native_destroy_tx(&bus, phy) == 0);
		}
	}
	for (unsigned fault = 0; fault < 6; fault++) {
		phy = fastlock_bootstrap(&bus);
		assert(ad9361_native_fastlock_prepare_tx(&bus, phy, frequencies, 2) == 0);
		assert(ad9361_native_fastlock_stage_tx(&bus, phy, 1) == 0);
		hw.corrupt_fast = fault == 0;
		hw.corrupt_setup = fault == 1;
		hw.lose_lock_on_recall = fault == 2;
		hw.bad_tx_pll = fault == 5;
		hw.corrupt_recall = fault == 4;
		if (fault == 3)
			phy->fastlock.current_profile[1] = 2;
		before = hw.transfers;
		error = fault == 0 ? ad9361_native_fastlock_stage_tx(&bus, phy, 1) :
		    ad9361_native_fastlock_recall_tx(&bus, phy, 1);
		assert(error == (fault == 2 ? ETIMEDOUT : EIO));
		if (fault == 2)
			assert(hw.transfers - before == counts[2] + 20);
		if (fault == 5)
			assert(hw.transfers - before == 2);
		assert(!bus.reset_released && bus.tx_fastlock == NULL);
		assert(ad9361_native_destroy_tx(&bus, phy) == 0);
	}
	phy = fastlock_bootstrap(&bus);
	hw.corrupt_fast = true;
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, frequencies, 2) == EIO);
	assert(bus.tx_fastlock == NULL && !bus.reset_released);
	assert(ad9361_native_destroy_tx(&bus, phy) == 0);
	phy = fastlock_bootstrap(&bus);
	for (unsigned i = 0; i < __arraycount(many); i++)
		many[i] = frequencies[0] + i * 500000ULL;
	assert(ad9361_native_fastlock_prepare_tx(&bus, phy, many, __arraycount(many)) == 0);
	assert(ad9361_native_fastlock_stage_tx(&bus, phy, 127) == 0);
	assert(ad9361_native_fastlock_recall_tx(&bus, phy, 127) == 0);
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.frequency_hz > many[127] - 100 && status.frequency_hz < many[127] + 100);
	assert(ad9361_native_destroy_tx(&bus, phy) == 0);
	printf("PASS fastlock 128-profile bounds, inactive-bank staging, exact recall, regular retune, readback/lock faults and every SPI failure (%u/%u/%u/%u transfers); no RF simulated\n",
	    counts[0], counts[1], counts[2], counts[3]);
}

static void test_tx(void)
{
	AD9361_InitParam p;
	struct ad9361_native_bus bus;
	struct ad9361_rf_phy *phy = NULL;
	struct ad9361_native_tx_status status;
	unsigned count, before;
	int error;

	tx_fdt = true;
	assert(ad9361_native_params_from_fdt(1, 40000000, &p) == EOPNOTSUPP);
	conflicting_fdt = true;
	assert(ad9361_native_params_from_fdt_tx(1, 40000000, &p) == EOPNOTSUPP);
	conflicting_fdt = false;
	assert(ad9361_native_params_from_fdt_tx(1, 40000000, &p) == 0);
	assert(!p.rx_only && p.native_tx_fixed_delays);
	memset(&hw, 0, sizeof(hw));
	hw.tx = true;
	ad9361_native_bus_init(&bus, &hw, spi, reset);
	assert(ad9361_native_bootstrap_tx(&bus, &p, 2400000000ULL, &phy) == EOPNOTSUPP);
	assert(hw.transfers == 0 && hw.resets == 0);
	ad9361_native_bus_init_tx(&bus, &hw, spi, reset);
	error = ad9361_native_bootstrap_tx(&bus, &p, 2400000000ULL, &phy);
	printf("TX bootstrap %d sticky %d transfers %u\n", error, bus.first_error, hw.transfers);
	assert(error == 0);
	count = hw.transfers;
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.sample_rate_hz == 13000000);
	assert(status.attenuation_mdB[0] == 89750 && status.attenuation_mdB[1] == 89750);
	assert(status.pll_locked && status.bbpll_locked && status.quadrature_calibrated &&
	    !status.digital_tuning_valid);
	assert(status.tx_delay == 0x70 && status.rx_delay == 4);
	assert(ad9361_native_set_tx_attenuation(&bus, phy, 30001) == EINVAL);
	assert(ad9361_native_set_tx_attenuation(&bus, phy, 30000) == 0);
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.attenuation_mdB[0] == 30000 && status.attenuation_mdB[1] == 89750);
	assert(ad9361_native_mute_tx(&bus, phy) == 0);
	assert(ad9361_native_retune_rx(&bus, phy, 2400000000ULL) == EPERM);
	assert(ad9361_native_retune_tx(&bus, phy, 2440000000ULL) == 0);
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.frequency_hz > 2439999900ULL && status.frequency_hz < 2440000100ULL);
	/* A changed hardware register must be reflected without a setter. */
	hw.reg[REG_TX1_ATTEN_0] = 100;
	hw.reg[REG_TX1_ATTEN_1] = 0;
	assert(ad9361_native_read_tx_status(&bus, phy, &status) == 0);
	assert(status.attenuation_mdB[0] == 25000);
	hw.corrupt_atten = true;
	assert(ad9361_native_set_tx_attenuation(&bus, phy, 30000) == EIO);
	assert(!bus.reset_released && bus.first_error == EIO);
	before = hw.transfers;
	assert(ad9361_native_mute_tx(&bus, phy) == EIO);
	assert(hw.transfers == before);
	hw.fail_reset = true;
	assert(ad9361_native_destroy_tx(&bus, phy) == EIO);
	phy = NULL;
	for (unsigned i = 1; i <= count; i++) {
		memset(&hw, 0, sizeof(hw));
		hw.tx = true;
		hw.fail_at = i;
		phy = NULL;
		ad9361_native_bus_init_tx(&bus, &hw, spi, reset);
		error = ad9361_native_bootstrap_tx(&bus, &p, 2400000000ULL, &phy);
		if (error != EIO || phy != NULL || bus.first_error != EIO || bus.reset_released) {
			fprintf(stderr, "TX failure %u errno %d sticky %d phy %p\n", i, error, bus.first_error, (void *)phy);
			abort();
		}
		assert(hw.transfers == i);
	}
	memset(&hw, 0, sizeof(hw));
	hw.tx = true;
	hw.bad_tx_pll = true;
	ad9361_native_bus_init_tx(&bus, &hw, spi, reset);
	assert(ad9361_native_bootstrap_tx(&bus, &p, 2400000000ULL, &phy) != 0);
	assert(!bus.reset_released && phy == NULL);
	assert(ad9361_native_destroy_tx(&bus, NULL) == 0);
	memset(&hw, 0, sizeof(hw));
	hw.tx = true;
	hw.bad_quad = true;
	ad9361_native_bus_init_tx(&bus, &hw, spi, reset);
	assert(ad9361_native_bootstrap_tx(&bus, &p, 2400000000ULL, &phy) == EIO);
	assert(!bus.reset_released && phy == NULL);
	printf("PASS TX explicit opt-in, fixed-delay bootstrap, fresh status, attenuation, mute, retune and every SPI failure (%u transfers); no RF simulated\n", count);
}

static void
test_one_rx(void)
{
	AD9361_InitParam p, invalid;
	struct ad9361_native_bus bus;
	struct ad9361_rf_phy *phy = NULL;
	struct adi_axi_rx_status status;
	struct adi_axi_rx_config cfg = {
	    .frequency_hz = 915000000ULL,
	    .sample_rate_hz = 10000000,
	    .bandwidth_hz = 8000000,
	    .rx_mask = 1, .channel_mask = 3,
	    .gain_mode = {ADI_AXI_GAIN_SLOW, ADI_AXI_GAIN_SLOW}};
	struct adi_axi_adc_io io = {.cookie = &hw,
	    .reset = adc_reset, .interface = adc_interface,
	    .channels = adc_channels, .pn_select = adc_pn_select,
	    .pn_status = adc_pn_status};
	unsigned before;

	one_rx_fdt = true;
	tx_fdt = false;
	assert(ad9361_native_params_from_fdt(1, 40000000, &p) == 0);
	assert(!p.two_rx_two_tx_mode_enable && !p.lvds_mode_enable);
	assert(p.dev_sel == ID_AD9363A);
	assert(p.one_rx_one_tx_mode_use_rx_num == 1);
	memset(&hw, 0, sizeof(hw));
	ad9361_native_bus_init(&bus, &hw, spi, reset);
	invalid = p;
	invalid.one_rx_one_tx_mode_use_rx_num = 2;
	assert(ad9361_native_bootstrap(&bus, &invalid, &cfg, &phy) == EOPNOTSUPP);
	assert(phy == NULL && hw.transfers == 0 && hw.resets == 0);
	invalid = p;
	invalid.two_rx_two_tx_mode_enable = true;
	assert(ad9361_native_bootstrap(&bus, &invalid, &cfg, &phy) == EINVAL);
	assert(phy == NULL && hw.transfers == 0 && hw.resets == 0);
	cfg.bandwidth_hz = 25000000;
	assert(ad9361_native_bootstrap(&bus, &p, &cfg, &phy) == EINVAL);
	assert(phy == NULL && hw.transfers == 0 && hw.resets == 0);
	cfg.bandwidth_hz = 8000000;
	assert(ad9361_native_bootstrap(&bus, &p, &cfg, &phy) == 0);
	assert(phy != NULL && !phy->pdata->rx2tx2);
	assert(ad9361_native_configure(&bus, phy, &cfg, &io) == 0);
	assert(ad9361_native_retune_rx(&bus, phy, 918000000ULL) == 0);
	assert(ad9361_native_read_rx_status(&bus, phy, &status) == 0);
	assert(status.valid_mask == 1);
	assert(hw.reg[REG_TX_ENABLE_FILTER_CTRL] < 0x40);
	assert((hw.reg[REG_TX_SYNTH_POWER_DOWN_OVERRIDE] & 31) == 31);
	before = hw.transfers;
	cfg.bandwidth_hz = 25000000;
	assert(ad9361_native_configure(&bus, phy, &cfg, &io) == EINVAL);
	assert(hw.transfers == before);
	cfg.bandwidth_hz = 8000000;
	assert(ad9361_native_retune_rx(&bus, phy, 100000000ULL) == EINVAL);
	assert(hw.transfers == before);
	cfg.rx_mask = 3; cfg.channel_mask = 15;
	assert(ad9361_native_configure(&bus, phy, &cfg, &io) == EINVAL);
	assert(hw.transfers == before);
	cfg.rx_mask = 1; cfg.channel_mask = 15;
	assert(ad9361_native_configure(&bus, phy, &cfg, &io) == EINVAL);
	assert(hw.transfers == before);
	ad9361_remove(phy);
	tx_fdt = true;
	assert(ad9361_native_params_from_fdt_tx(1, 40000000, &p) == EOPNOTSUPP);
	one_rx_fdt = false;
	tx_fdt = false;
	memset(&hw, 0, sizeof(hw));
	puts("PASS RX1 1R1T CMOS FDT, bootstrap, configure, retune, status, "
	    "topology refusal and TX inhibition; no RF simulated");
}

static void
test_extended_rx(void)
{
	AD9361_InitParam p;
	struct ad9361_native_bus bus;
	struct ad9361_rf_phy *phy = NULL;
	struct adi_axi_rx_config cfg = {
	    .frequency_hz = 137900000ULL, .sample_rate_hz = 10000000,
	    .bandwidth_hz = 8000000, .rx_mask = 1, .channel_mask = 3,
	    .gain_mode = {ADI_AXI_GAIN_SLOW, ADI_AXI_GAIN_SLOW}};
	struct adi_axi_adc_io io = {.cookie = &hw,
	    .reset = adc_reset, .interface = adc_interface,
	    .channels = adc_channels, .pn_select = adc_pn_select,
	    .pn_status = adc_pn_status};
	const uint64_t targets[] = {
	    70000000ULL, 137100000ULL, 137900000ULL, 6000000000ULL};
	unsigned before;

	one_rx_fdt = true;
	extended_rx_fdt = false;
	memset(&hw, 0, sizeof(hw));
	ad9361_native_bus_init(&bus, &hw, spi, reset);
	assert(ad9361_native_params_from_fdt(1, 40000000, &p) == 0);
	assert(ad9361_native_bootstrap(&bus, &p, &cfg, &phy) == EINVAL);
	assert(phy == NULL && hw.transfers == 0 && hw.resets == 0);
	extended_rx_fdt = true;
	assert(ad9361_native_params_from_fdt(1, 40000000, &p) == 0);
	assert(ad9361_native_bootstrap(&bus, &p, &cfg, &phy) == 0);
	assert(phy != NULL && phy->dev_sel == ID_AD9363A);
	assert(ad9361_native_configure(&bus, phy, &cfg, &io) == 0);
	for (unsigned i = 0; i < __arraycount(targets); i++) {
		assert(ad9361_native_retune_rx(&bus, phy, targets[i]) == 0);
		assert(hw.reg[REG_TX_ENABLE_FILTER_CTRL] < 0x40);
		assert((hw.reg[REG_TX_SYNTH_POWER_DOWN_OVERRIDE] & 31) == 31);
	}
	before = hw.transfers;
	assert(ad9361_native_retune_rx(&bus, phy, 69999999ULL) == EINVAL);
	assert(ad9361_native_retune_rx(&bus, phy, 6000000001ULL) == EINVAL);
	assert(ad9361_validate_rfpll(phy, true, 6000000000ULL) == -EINVAL);
	cfg.bandwidth_hz = 20000001;
	assert(ad9361_native_configure(&bus, phy, &cfg, &io) == EINVAL);
	assert(hw.transfers == before);
	ad9361_remove(phy);
	/* The RX experiment must not enter the independently opted-in TX path. */
	one_rx_fdt = false;
	tx_fdt = true;
	assert(ad9361_native_params_from_fdt_tx(1, 40000000, &p) == EOPNOTSUPP);
	extended_rx_fdt = false;
	tx_fdt = false;
	memset(&hw, 0, sizeof(hw));
	puts("PASS explicit AD9363A extended RX, unchanged identity and bandwidth, "
	    "70/137/6000 MHz retunes, bounds and TX refusal; no RF simulated");
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	AD9361_InitParam p;
	struct ad9361_rf_phy *phy = NULL;
	struct ad9361_native_bus bus;
	struct adi_axi_rx_config cfg = {
	    .frequency_hz = 2400000000ULL,
	    .sample_rate_hz = 12000000,
	    .bandwidth_hz = 10000000,
	    .rf_port = 0,
	    .rx_mask = 3,
	    .channel_mask = 15,
	    .gain_mode = {ADI_AXI_GAIN_SLOW, ADI_AXI_GAIN_SLOW}};
	struct adi_axi_adc_io io = {.cookie = &hw,
				    .reset = adc_reset,
				    .interface = adc_interface,
				    .channels = adc_channels,
				    .pn_select = adc_pn_select,
				    .pn_status = adc_pn_status};
	int r;

	test_one_rx();
	test_extended_rx();
	invalid_aux_rate = true;
	assert(ad9361_native_params_from_fdt(1, 40000000, &p) == EINVAL);
	invalid_aux_rate = false;
	r = ad9361_native_params_from_fdt(1, 40000000, &p);
	printf("params %d lvds=%u rxclks=%u tx=%u\n", r, p.lvds_mode_enable,
	       p.rx_path_clock_frequencies[5], p.tx_path_clock_frequencies[5]);
	assert(r == 0);
	{
		AD9361_InitParam invalid = p;
		invalid.aux_adc_rate = 0;
		ad9361_native_bus_init(&bus, &hw, spi, reset);
		assert(ad9361_native_bootstrap(&bus, &invalid, &cfg, &phy) ==
		    EINVAL);
		assert(phy == NULL && hw.transfers == 0 && hw.resets == 0);
		invalid = p;
		invalid.rssi_duration = 0;
		assert(ad9361_native_bootstrap(&bus, &invalid, &cfg, &phy) ==
		    EINVAL);
		assert(phy == NULL && hw.transfers == 0 && hw.resets == 0);
	}
	ad9361_native_bus_init(&bus, &hw, spi, reset);
	r = ad9361_native_bootstrap(&bus, &p, &cfg, &phy);
	printf("bootstrap ret=%d first_error=%d SPI=%u writes=%u resets=%u "
	       "phy=%p\n",
	       r, bus.first_error, hw.transfers, hw.writes, hw.resets,
	       (void *)phy);
	assert(r == 0);
	assert(hw.pn_reads == 0);
	unsigned bootstrap_transfers = hw.transfers;
	r = ad9361_native_configure(&bus, phy, &cfg, &io);
	printf("configure ret=%d first_error=%d SPI=%u writes=%u PNreads=%u\n",
	       r, bus.first_error, hw.transfers, hw.writes, hw.pn_reads);
	assert(r == 0);
	assert(hw.pn_reads > 1024);
	assert(hw.reg[REG_TX_ENABLE_FILTER_CTRL] < 0x40);
	assert((hw.reg[REG_TX_SYNTH_POWER_DOWN_OVERRIDE] & 31) == 31);
	ad9361_remove(phy);
	for (unsigned i = 1; i <= bootstrap_transfers; i++) {
		memset(&hw, 0, sizeof(hw));
		hw.fail_at = i;
		phy = NULL;
		ad9361_native_bus_init(&bus, &hw, spi, reset);
		r = ad9361_native_bootstrap(&bus, &p, &cfg, &phy);
		if (r != EIO || phy != NULL || bus.first_error != EIO) {
			fprintf(
			    stderr,
			    "failure injection %u: errno%d sticky%d phy%p\\n",
			    i, r, bus.first_error, (void *)phy);
			abort();
		}
	}
	for (unsigned kind = 0; kind < 2; kind++) {
		memset(&hw, 0, sizeof(hw));
		hw.bad_cal = kind == 0;
		hw.bad_pll = kind == 1;
		phy = NULL;
		ad9361_native_bus_init(&bus, &hw, spi, reset);
		r = ad9361_native_bootstrap(&bus, &p, &cfg, &phy);
		assert(r == (kind == 0 ? EIO : ETIMEDOUT));
		assert(phy == NULL);
	}
	printf("PASS every bootstrap SPI failure (%u transfers), invalid RX "
	       "calibration and BBPLL lock timeout propagate without "
	       "publishing PHY\n",
	       bootstrap_transfers);
	puts("PASS full native bootstrap/configure transaction trace with "
	     "generic synthetic FDT properties; no RF behavior is simulated or "
	     "claimed");
	test_tx();
	test_fastlock();
	return 0;
}
