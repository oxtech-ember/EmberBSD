/* SPDX-License-Identifier: BSD-2-Clause */
/* Generic ADI converter and AXI ADC capture ownership interface. */
#ifndef _DEV_FDT_ADI_AXI_ADCVAR_H_
#define _DEV_FDT_ADI_AXI_ADCVAR_H_

#include <sys/types.h>
#include <sys/device.h>

struct adi_axi_adc_lease;
struct adi_axi_converter;

enum adi_axi_gain_mode {
	ADI_AXI_GAIN_MANUAL,
	ADI_AXI_GAIN_SLOW,
	ADI_AXI_GAIN_FAST
};

struct adi_axi_rx_config {
	uint64_t frequency_hz;
	uint32_t sample_rate_hz;
	uint32_t bandwidth_hz;
	uint32_t rf_port;
	uint32_t rx_mask;
	uint32_t channel_mask;
	int32_t gain_mdB[2];
	enum adi_axi_gain_mode gain_mode[2];
};

/* Only valid during the converter's configure callback. The ADC owns MMIO.
 * reset() requires the consumer's successful quiesce and active RX clocks.
 * Channel format bits: sign-extension 0x40, offset-binary 0x20, format 0x10,
 * IQ correction 0x200, DC filter 0x100. No arbitrary register access exists.
 */
struct adi_axi_adc_io {
	void *cookie;
	int (*reset)(void *);
	int (*channels)(void *, uint32_t, uint32_t);
	/* Digital RX framing/rate only, no DDS or analog transmitter control. */
	int (*interface)(void *, bool two_rx, bool lvds, bool half_rate);
	int (*pn_select)(void *, unsigned int, unsigned int);
	/* Returns EIO if the ADC link-valid status is not asserted. */
	int (*pn_status)(void *, unsigned int, bool, uint32_t *);
};

/* Read-only receiver measurements, relative RSSI in milli-dB (not dBm).
 * A bit in valid_mask certifies both measurements for the corresponding RX.
 */
struct adi_axi_rx_status {
	uint32_t valid_mask;
	int32_t rssi_mdB[2];
	int32_t gain_mdB[2];
};

struct adi_axi_converter_ops {
	/* Optional one-shot cold clock setup. No ADC MMIO, capture start or DMA
	 * publication is permitted. Return only after real clock/PLL readback;
	 * failure must leave capture disabled. Normal configure still follows.
	 */
	int (*bootstrap)(void *, const struct adi_axi_rx_config *);
	int (*read_rx_status)(void *, struct adi_axi_rx_status *);
	/* Frequency only; caller holds an idle configured RX lease. */
	int (*retune_rx)(void *, uint64_t);
	/* Must return EOPNOTSUPP until real converter initialization exists. */
	int (*configure)(void *, const struct adi_axi_rx_config *,
	    const struct adi_axi_adc_io *);
};

struct adi_axi_capture_ops {
	/* Optional one-shot proof of a freshly reset, never-started pipeline.
	 * A trusted reset/load handoff plus exclusive admission must prove no
	 * previous DMA ownership; reset bits or a delay are not that proof.
	 * Consume the handoff even on failure. This is NOT a drain certificate.
	 * Must not publish DMA addresses, start capture or re-enter this API.
	 */
	int (*cold_bootstrap)(void *);
	/* Thread context. Success proves every consumer AXI transaction drained.
	 * Must not re-enter the lease API; must keep required clocks running.
	 */
	int (*quiesce)(void *, unsigned int timeout_ms);
};

/* Registration owns a device reference. unregister returns EBUSY while leased.
 * Converter entry points that disrupt clocks/reset/RF must use control_enter
 * and control_exit, or execute exclusively inside configure above.
 */
int adi_axi_converter_register(device_t, int,
	const struct adi_axi_converter_ops *, void *, struct adi_axi_converter **);
int adi_axi_converter_unregister(struct adi_axi_converter *);
int adi_axi_converter_control_enter(struct adi_axi_converter *);
void adi_axi_converter_control_exit(struct adi_axi_converter *);

/* All operations below are synchronous, thread context, positive errno.
 * get acquires one exclusive lease; no physical addresses enter this API.
 * Failed quiesce poisons the lease permanently: buffers, clocks and provider
 * references must remain pinned until a coordinated platform restart.
 */
int adi_axi_adc_acquire(int phandle, device_t consumer,
	const struct adi_axi_capture_ops *, void *, struct adi_axi_adc_lease **);
/* Establish RX clocks after a trusted cold handoff, or a positive drain with
 * existing clocks. Require a real drain after clock setup in both cases.
 * Success makes the lease quiet, not configured.
 * One attempt per converter registration; no warm-session recovery path.
 * Any attempted proof/setup/drain failure poisons and retains the lease.
 */
int adi_axi_adc_bootstrap(struct adi_axi_adc_lease *,
	const struct adi_axi_rx_config *, unsigned int timeout_ms);
int adi_axi_adc_configure(struct adi_axi_adc_lease *,
	const struct adi_axi_rx_config *, unsigned int timeout_ms);
int adi_axi_adc_read_rx_status(struct adi_axi_adc_lease *,
	struct adi_axi_rx_status *);
int adi_axi_adc_retune_rx(struct adi_axi_adc_lease *, uint64_t);
int adi_axi_adc_start(struct adi_axi_adc_lease *);
int adi_axi_adc_stop(struct adi_axi_adc_lease *, unsigned int timeout_ms);
int adi_axi_adc_release(struct adi_axi_adc_lease *);
void adi_axi_adc_poison(struct adi_axi_adc_lease *);

#endif
