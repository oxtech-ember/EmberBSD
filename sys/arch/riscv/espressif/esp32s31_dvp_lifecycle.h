/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _RISCV_ESPRESSIF_ESP32S31_DVP_LIFECYCLE_H_
#define _RISCV_ESPRESSIF_ESP32S31_DVP_LIFECYCLE_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdbool.h>
#include <stdint.h>
#endif

enum esp32s31_dvp_phase {
	ESP32S31_DVP_IDLE,
	ESP32S31_DVP_WAIT_DMA,
	ESP32S31_DVP_RESULT_READY,
	ESP32S31_DVP_DMA_QUIESCED,
};

/* Caller serializes access to this state. */
struct esp32s31_dvp_lifecycle {
	uint64_t generation;
	enum esp32s31_dvp_phase phase;
	bool streaming;
	bool stopping;
	bool stop_busy;
	bool sensor_streaming;
	bool dma_active;
};

static inline bool
esp32s31_dvp_lifecycle_start_allowed(const struct esp32s31_dvp_lifecycle *lc)
{
	return !lc->streaming && !lc->stopping && !lc->sensor_streaming &&
	    !lc->dma_active && !lc->stop_busy;
}

static inline bool
esp32s31_dvp_lifecycle_stop_acquire(struct esp32s31_dvp_lifecycle *lc)
{
	if (lc->stop_busy)
		return false;
	lc->stop_busy = true;
	return true;
}

static inline void
esp32s31_dvp_lifecycle_stop_release(struct esp32s31_dvp_lifecycle *lc)
{
	lc->stop_busy = false;
}

static inline uint64_t
esp32s31_dvp_lifecycle_begin_stream(struct esp32s31_dvp_lifecycle *lc)
{
	if (++lc->generation == 0)
		++lc->generation;
	lc->phase = ESP32S31_DVP_WAIT_DMA;
	lc->streaming = true;
	return lc->generation;
}

static inline bool esp32s31_dvp_lifecycle_completion_current(
	const struct esp32s31_dvp_lifecycle *, uint64_t);

static inline bool
esp32s31_dvp_lifecycle_result_ready(struct esp32s31_dvp_lifecycle *lc,
	uint64_t generation)
{
	if (!esp32s31_dvp_lifecycle_completion_current(lc, generation) ||
	    lc->phase != ESP32S31_DVP_WAIT_DMA)
		return false;
	lc->phase = ESP32S31_DVP_RESULT_READY;
	return true;
}

static inline bool
esp32s31_dvp_lifecycle_dma_quiesced(struct esp32s31_dvp_lifecycle *lc,
	uint64_t generation)
{
	if (!esp32s31_dvp_lifecycle_completion_current(lc, generation) ||
	    lc->phase != ESP32S31_DVP_RESULT_READY)
		return false;
	lc->phase = ESP32S31_DVP_DMA_QUIESCED;
	return true;
}

static inline bool
esp32s31_dvp_lifecycle_rearm_allowed(
	const struct esp32s31_dvp_lifecycle *lc, uint64_t generation)
{
	return esp32s31_dvp_lifecycle_completion_current(lc, generation) &&
	    lc->phase == ESP32S31_DVP_DMA_QUIESCED;
}

static inline void
esp32s31_dvp_lifecycle_transfer_started(struct esp32s31_dvp_lifecycle *lc)
{
	lc->phase = ESP32S31_DVP_WAIT_DMA;
}

static inline bool
esp32s31_dvp_lifecycle_completion_current(
	const struct esp32s31_dvp_lifecycle *lc, uint64_t generation)
{
	return lc->streaming && !lc->stopping && generation == lc->generation;
}

static inline void
esp32s31_dvp_lifecycle_begin_stop(struct esp32s31_dvp_lifecycle *lc)
{
	lc->streaming = false;
	lc->stopping = true;
	lc->phase = ESP32S31_DVP_IDLE;
	if (++lc->generation == 0)
		++lc->generation;
}

static inline bool
esp32s31_dvp_lifecycle_finish_stop(struct esp32s31_dvp_lifecycle *lc)
{
	if (lc->dma_active || lc->sensor_streaming)
		return false;
	lc->stopping = false;
	return true;
}

#endif /* _RISCV_ESPRESSIF_ESP32S31_DVP_LIFECYCLE_H_ */
