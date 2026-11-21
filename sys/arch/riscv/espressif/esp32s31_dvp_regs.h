/* $NetBSD$ */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _RISCV_ESPRESSIF_ESP32S31_DVP_REGS_H_
#define _RISCV_ESPRESSIF_ESP32S31_DVP_REGS_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#endif

#define ESP32S31_DVP_CAM_STOP_EN	(1U << 0)
#define ESP32S31_DVP_CAM_VS_EOF_EN	(1U << 8)
#define ESP32S31_DVP_CAM_UPDATE_REG	(1U << 4)
#define ESP32S31_DVP_CAM_START		(1U << 29)

/* Match ESP-IDF cam_hal_init(): DMA EOF is VSYNC driven, not FIFO-stop. */
static inline uint32_t
esp32s31_dvp_capture_control(void)
{
	return ESP32S31_DVP_CAM_VS_EOF_EN | ESP32S31_DVP_CAM_UPDATE_REG;
}

/* cam_ll_stop(): clear START and latch the new CAM control state. */
static inline uint32_t
esp32s31_dvp_stop_control1(uint32_t control1)
{
	return control1 & ~ESP32S31_DVP_CAM_START;
}

static inline uint32_t
esp32s31_dvp_latch_control(uint32_t control)
{
	return control | ESP32S31_DVP_CAM_UPDATE_REG;
}

#endif /* _RISCV_ESPRESSIF_ESP32S31_DVP_REGS_H_ */
