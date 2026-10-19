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
/* Origin: EmberBSD AD9361 hardware status and exclusive TX control interface. */

#ifndef _DEV_SPI_AD9361VAR_H_
#define _DEV_SPI_AD9361VAR_H_

#include <sys/types.h>
#include <sys/device.h>

/* Raw register values from SPI, never the no-OS driver's cached state. */
struct ad9361_status {
	uint8_t product_id;
	uint8_t tx_enable;
	uint8_t rx_enable;
	uint8_t ensm_mode;
	uint8_t ensm_config1;
	uint8_t ensm_config2;
	uint8_t state;
	uint8_t tx_synth_powerdown;
	uint8_t tx1_atten[2];
	uint8_t tx2_atten[2];
};

int ad9361_read_status(device_t, struct ad9361_status *);

struct ad9361_tx;

struct ad9361_tx_status {
	uint64_t frequency_hz;
	uint32_t sample_rate_hz;
	uint32_t attenuation_mdB[2];
	uint8_t ensm_state;
	uint8_t tx_enable;
	uint8_t rx_delay;
	uint8_t tx_delay;
	bool pll_locked;
	bool bbpll_locked;
	bool quadrature_calibrated;
	bool digital_tuning_valid;
};

/*
 * Sleepable control API. The provider is selected by its AD9361 FDT phandle.
 * The non-NULL owner must match on every call and keep the handle private.
 * Acquire grants exclusive access, but never releases reset or enables RF.
 *
 * The consumer must hold an independent hardware TX/reset deadline and keep
 * DAC samples zero before prepare_reset and throughout bootstrap or retune.
 * Arm that deadline, release the GPIO with prepare_reset, then bootstrap.
 * Prepare probes at no more than 1 MHz. It then verifies the FDT SPI rate
 * with a second product ID read before allowing bootstrap. RX keeps its rate.
 * The hardware gate must not cancel its deadline on the no-OS GPIO reset
 * pulse. Bootstrap performs the full reset and finishes at maximum attenuation.
 *
 * Stop DAC/RF in the independent hardware gate before mute or release.
 * Mute only requests maximum attenuation; it does not stop the hardware gate.
 * Release asserts GPIO reset, destroys native state, and relinquishes ownership.
 * A reset readback failure poisons the provider until device reattachment.
 * Any failed native operation invalidates the PHY; release the lease before
 * retrying. A lease permits only one bootstrap attempt.
 */
int ad9361_tx_acquire(int, void *, struct ad9361_tx **);
int ad9361_tx_prepare_reset(struct ad9361_tx *, void *);
int ad9361_tx_bootstrap(struct ad9361_tx *, void *, uint64_t);
int ad9361_tx_retune(struct ad9361_tx *, void *, uint64_t);
/*
 * Prepare caches 1..128 frequencies and finishes at frequencies[0]. Keep DAC
 * samples zero and attenuation at maximum throughout preparation. Frequencies
 * must remain within the no-OS quadrature calibration range from index zero.
 * Stage loads and verifies an inactive bank; current DAC traffic may continue.
 * Recall consumes exactly the staged index and requires zero DAC samples.
 * Ordinary retune discards the cache. Neither operation proves RF acceptance.
 */
int ad9361_tx_lease_fastlock_prepare(struct ad9361_tx *, void *, const uint64_t *, size_t);
int ad9361_tx_lease_fastlock_stage(struct ad9361_tx *, void *, unsigned int);
int ad9361_tx_lease_fastlock_recall(struct ad9361_tx *, void *, unsigned int);
int ad9361_tx_set_attenuation(struct ad9361_tx *, void *, uint32_t);
int ad9361_tx_lease_mute(struct ad9361_tx *, void *);
int ad9361_tx_read_status(struct ad9361_tx *, void *, struct ad9361_tx_status *);
int ad9361_tx_release(struct ad9361_tx *, void *);

#endif
