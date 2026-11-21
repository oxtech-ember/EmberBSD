/* $NetBSD$ */
/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_ESP32S31_DVP_FRAME_H_
#define _RISCV_ESPRESSIF_ESP32S31_DVP_FRAME_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#endif

#define ESP32S31_DVP_FRAME_TAIL_SIZE 64

enum esp32s31_dvp_frame_status {
	ESP32S31_DVP_FRAME_INVALID,
	ESP32S31_DVP_FRAME_PARTIAL,
	ESP32S31_DVP_FRAME_COMPLETE,
};

/* A short successful EOF is a warm-up frame, not a fatal capture error. */
static inline enum esp32s31_dvp_frame_status
esp32s31_dvp_frame_classify(size_t frame_size, size_t dma_size,
    size_t received_size, size_t *payload_size)
{
	if (frame_size == 0 || dma_size < frame_size ||
	    frame_size > SIZE_MAX - ESP32S31_DVP_FRAME_TAIL_SIZE ||
	    dma_size < frame_size + ESP32S31_DVP_FRAME_TAIL_SIZE ||
	    received_size == 0 || received_size > dma_size ||
	    payload_size == NULL)
		return ESP32S31_DVP_FRAME_INVALID;
	if (received_size < frame_size)
		return ESP32S31_DVP_FRAME_PARTIAL;

	*payload_size = frame_size;
	return ESP32S31_DVP_FRAME_COMPLETE;
}

#endif /* _RISCV_ESPRESSIF_ESP32S31_DVP_FRAME_H_ */
