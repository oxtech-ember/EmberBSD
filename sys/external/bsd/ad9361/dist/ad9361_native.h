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

/* Origin: EmberBSD adapter for ADI no-OS
 * ce7107f957d19858f2dffc85bf3d7f2c70367000. */
#ifndef _AD9361_NATIVE_H_
#define _AD9361_NATIVE_H_

#include <sys/types.h>
#include "no_os_spi.h"
#include "no_os_gpio.h"
#include "ad9361_api.h"
#include <dev/fdt/adi_axi_adcvar.h>

struct ad9361_rf_phy;
struct ad9361_native_fastlock;
struct ad9361_native_bus {
	void *cookie;
	/* Transfer mutates the buffer with full-duplex receive data. */
	int (*transfer)(void *, uint8_t *, size_t);
	/* Set physical RESETB and verify pin readback; value 1 releases reset.
	 */
	int (*reset)(void *, bool);
	struct no_os_spi_desc spi;
	struct no_os_gpio_desc gpio;
	int first_error;
	bool reset_released;
	/* Selected only by the separate TX initializer. */
	bool tx_enabled;
	/* Private cache belongs to this TX PHY and is discarded on retune. */
	struct ad9361_native_fastlock *tx_fastlock;
};

void ad9361_native_bus_init(struct ad9361_native_bus *, void *,
			    int (*)(void *, uint8_t *, size_t),
			    int (*)(void *, bool));
void ad9361_native_bus_init_tx(struct ad9361_native_bus *, void *,
	int (*)(void *, uint8_t *, size_t), int (*)(void *, bool));
int ad9361_native_bus_error(const struct ad9361_native_bus *);
int ad9361_native_bus_abort(struct ad9361_native_bus *);
int ad9361_native_check_write(unsigned int, uint8_t);
/* Caller owns an exclusive capture lease and transport resources throughout. */
int ad9361_native_bootstrap(struct ad9361_native_bus *,
			    const AD9361_InitParam *,
			    const struct adi_axi_rx_config *,
			    struct ad9361_rf_phy **);
int ad9361_native_configure(struct ad9361_native_bus *, struct ad9361_rf_phy *,
			    const struct adi_axi_rx_config *,
			    const struct adi_axi_adc_io *);
int ad9361_native_read_rx_status(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, struct adi_axi_rx_status *);
/* Recalculate the RX sample clock from hardware, without cached parents. */
int ad9361_native_read_rx_clock(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, uint32_t *);
int ad9361_native_retune_rx(struct ad9361_native_bus *,
    struct ad9361_rf_phy *, uint64_t);
int ad9361_native_params_from_fdt(int, uint32_t, AD9361_InitParam *);
/* TX calls require an exclusive lease, a hardware reset deadline and zero DAC
 * samples during bootstrap, mute and retune. Fixed delay taps are not a
 * digital-interface calibration. No call in this interface qualifies RF.
 */
#define AD9361_NATIVE_TX_RATE 13000000U
#define AD9361_NATIVE_TX_BANDWIDTH 2000000U
#define AD9361_NATIVE_TX_MAX_ATTENUATION 89750U
#define AD9361_NATIVE_TX_FASTLOCK_MAX 128U
struct ad9361_native_tx_status {
	uint64_t frequency_hz;
	uint32_t sample_rate_hz;
	uint32_t attenuation_mdB[2];
	uint8_t ensm_state, tx_enable, rx_delay, tx_delay;
	bool pll_locked, bbpll_locked, quadrature_calibrated;
	bool digital_tuning_valid;
};
int ad9361_native_params_from_fdt_tx(int, uint32_t, AD9361_InitParam *);
int ad9361_native_bootstrap_tx(struct ad9361_native_bus *,
	const AD9361_InitParam *, uint64_t, struct ad9361_rf_phy **);
int ad9361_native_retune_tx(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, uint64_t);
/* Prepare and recall require zero DAC samples. Prepare also requires maximum
 * attenuation and finishes at frequencies[0]. All frequencies must fit within
 * the no-OS quadrature calibration range from frequencies[0]. Stage can run
 * during transmission; it writes only the inactive hardware profile bank.
 * Recall accepts exactly the staged index and consumes it on success.
 */
int ad9361_native_fastlock_prepare_tx(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, const uint64_t *, size_t);
int ad9361_native_fastlock_stage_tx(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, unsigned int);
int ad9361_native_fastlock_recall_tx(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, unsigned int);
int ad9361_native_set_tx_attenuation(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, uint32_t);
int ad9361_native_mute_tx(struct ad9361_native_bus *, struct ad9361_rf_phy *);
int ad9361_native_destroy_tx(struct ad9361_native_bus *, struct ad9361_rf_phy *);
int ad9361_native_read_tx_status(struct ad9361_native_bus *,
	struct ad9361_rf_phy *, struct ad9361_native_tx_status *);
#endif
