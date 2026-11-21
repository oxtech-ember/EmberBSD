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

/* Accept complete uncompressed frames and discard any captured tail bytes. */
static inline int
esp32s31_dvp_frame_payload_size(size_t frame_size, size_t dma_size,
    size_t received_size, size_t *payload_size)
{
	if (frame_size == 0 || dma_size < frame_size ||
	    received_size < frame_size || received_size > dma_size ||
	    payload_size == NULL)
		return 0;

	*payload_size = frame_size;
	return 1;
}

#endif /* _RISCV_ESPRESSIF_ESP32S31_DVP_FRAME_H_ */
