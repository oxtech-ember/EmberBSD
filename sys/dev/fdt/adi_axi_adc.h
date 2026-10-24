/* SPDX-License-Identifier: BSD-2-Clause */
/* Public receive-only interface to the ADI AXI ADC provider. */
#ifndef _DEV_FDT_ADI_AXI_ADC_H_
#define _DEV_FDT_ADI_AXI_ADC_H_

#include <sys/types.h>
#include <sys/ioccom.h>

#define ADI_AXI_ADC_ABI_VERSION		1U
#define ADI_AXI_ADC_IQ_SAMPLE_BYTES	4U
#define ADI_AXI_ADC_CAPTURE_ALIGNMENT	8U
#define ADI_AXI_ADC_RX1_MASK		0x1U
#define ADI_AXI_ADC_RX1_IQ_CHANNEL_MASK	0x3U
#define ADI_AXI_ADC_CAPTURE_MAX_BYTES	1048576U

#define ADI_AXI_ADC_RX_COUNT		2U

enum adi_axi_adc_gain_mode {
	ADI_AXI_ADC_GAIN_MANUAL,
	ADI_AXI_ADC_GAIN_SLOW,
	ADI_AXI_ADC_GAIN_FAST
};

/* One open receive session. No transmit controls are part of this ABI. */
struct adi_axi_adc_config {
	uint32_t abi_version;
	uint32_t struct_size;
	uint64_t frequency_hz;
	uint32_t sample_rate_hz;
	uint32_t bandwidth_hz;
	uint32_t rf_port;
	uint32_t rx_mask;
	uint32_t channel_mask;
	int32_t gain_mdB[ADI_AXI_ADC_RX_COUNT];
	uint32_t gain_mode[ADI_AXI_ADC_RX_COUNT];
	uint32_t timeout_ms;
	uint32_t reserved[4];
};

/* read(2) returns exactly sample_count complex I/Q samples after capture. */
struct adi_axi_adc_capture {
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t sample_count;
	uint32_t timeout_ms;
	uint32_t reserved[4];
};

struct adi_axi_adc_status {
	uint32_t abi_version;
	uint32_t struct_size;
	uint32_t state;
	uint32_t valid_mask;
	int32_t rssi_mdB[ADI_AXI_ADC_RX_COUNT];
	int32_t gain_mdB[ADI_AXI_ADC_RX_COUNT];
	uint32_t captured_samples;
	int32_t last_capture_error;
	uint32_t reserved[4];
};

#define ADI_AXI_ADC_STATE_IDLE		0U
#define ADI_AXI_ADC_STATE_CONFIGURED	1U
#define ADI_AXI_ADC_STATE_CAPTURED	2U
#define ADI_AXI_ADC_STATE_ERROR		3U

#define ADI_AXI_ADCIOC_CONFIG	_IOW('A', 0x40, struct adi_axi_adc_config)
#define ADI_AXI_ADCIOC_CAPTURE	_IOW('A', 0x41, struct adi_axi_adc_capture)
#define ADI_AXI_ADCIOC_STATUS	_IOWR('A', 0x42, struct adi_axi_adc_status)

#endif /* _DEV_FDT_ADI_AXI_ADC_H_ */
