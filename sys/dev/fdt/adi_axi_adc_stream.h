/* SPDX-License-Identifier: BSD-2-Clause */
/* Additive receive streaming ABI. ABI1 remains unchanged. */
#ifndef _DEV_FDT_ADI_AXI_ADC_STREAM_H_
#define _DEV_FDT_ADI_AXI_ADC_STREAM_H_
#include <sys/types.h>
#include <sys/ioccom.h>
#define ADI_AXI_ADC_STREAM_ABI_VERSION 2U
#define ADI_AXI_ADC_STREAM_MAX_BLOCKS 16U
#define ADI_AXI_ADC_STREAM_MAX_BYTES 4194304U
#define ADI_AXI_ADC_STREAM_RUNNING 1U
#define ADI_AXI_ADC_STREAM_LOSS_UNKNOWN 2U
struct adi_axi_adc_stream_config {
 uint32_t abi_version;
 uint32_t struct_size;
 uint32_t block_bytes;
 uint32_t block_count;
 uint32_t reserved[4];
};
/* Raw reads never continue after known loss. EOVERFLOW requires STOP/START.
 * Sequence identifies the current block. block_offset locates partial reads.
 * A new START or CONFIG advances generation and invalidates all old bytes.
 * Dropped sample counts are unknown after hardware FIFO overflow or starvation.
 */
struct adi_axi_adc_stream_status {
 uint32_t abi_version;
 uint32_t struct_size;
 uint64_t generation;
 uint64_t sequence;
 uint64_t completed_blocks;
 uint64_t completed_bytes;
 uint64_t discontinuities;
 uint64_t dropped_blocks;
 uint32_t flags;
 int32_t last_error;
 uint32_t block_bytes;
 uint32_t block_count;
 uint32_t block_offset;
 uint32_t ready_blocks;
 uint32_t sample_rate_hz;
 uint32_t reserved[3];
};
#define ADI_AXI_ADCIOC_STREAM_START _IOW('A', 0x50, struct adi_axi_adc_stream_config)
#define ADI_AXI_ADCIOC_STREAM_STATUS _IOWR('A', 0x51, struct adi_axi_adc_stream_status)
#define ADI_AXI_ADCIOC_STREAM_STOP _IO('A', 0x52)
#endif
