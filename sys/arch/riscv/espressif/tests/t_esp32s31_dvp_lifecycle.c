/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include "../esp32s31_dvp_lifecycle.h"
#include "../esp32s31_dvp_regs.h"
#include "../esp32s31_dvp_frame.h"

int
main(void)
{
	struct esp32s31_dvp_lifecycle lc = { 0 };
	struct esp32s31_dvp_lifecycle retry = { 0 };
	uint64_t first, second;
	uint32_t cam_ctrl;
	size_t payload_size = 0;
	enum esp32s31_dvp_frame_status frame_status;

	cam_ctrl = esp32s31_dvp_capture_control();
	assert((cam_ctrl & ESP32S31_DVP_CAM_STOP_EN) == 0);
	assert((cam_ctrl & ESP32S31_DVP_CAM_VS_EOF_EN) != 0);
	assert((cam_ctrl & ESP32S31_DVP_CAM_UPDATE_REG) != 0);
	assert((esp32s31_dvp_stop_control1(ESP32S31_DVP_CAM_START | 0x10) &
	    ESP32S31_DVP_CAM_START) == 0);
	assert((esp32s31_dvp_latch_control(0x20) &
	    ESP32S31_DVP_CAM_UPDATE_REG) != 0);

	assert(esp32s31_dvp_lifecycle_start_allowed(&lc));
	first = esp32s31_dvp_lifecycle_begin_stream(&lc);
	/* CAM/DMA are armed before S_STREAM enables the sensor. */
	assert(!esp32s31_dvp_lifecycle_sensor_start_allowed(&lc));
	esp32s31_dvp_lifecycle_transfer_started(&lc);
	lc.dma_active = true;
	assert(esp32s31_dvp_lifecycle_sensor_start_allowed(&lc));
	lc.sensor_streaming = true;
	assert(!esp32s31_dvp_lifecycle_sensor_start_allowed(&lc));
	assert(esp32s31_dvp_lifecycle_completion_current(&lc, first));
	assert(!esp32s31_dvp_lifecycle_rearm_allowed(&lc, first));
	assert(esp32s31_dvp_lifecycle_result_ready(&lc, first));
	assert(!esp32s31_dvp_lifecycle_rearm_allowed(&lc, first));
	assert(esp32s31_dvp_lifecycle_dma_quiesced(&lc, first));
	assert(esp32s31_dvp_lifecycle_rearm_allowed(&lc, first));
	frame_status = esp32s31_dvp_frame_classify(614400, 614464, 399975,
	    &payload_size);
	assert(frame_status == ESP32S31_DVP_FRAME_PARTIAL);
	assert(payload_size == 0);
	esp32s31_dvp_lifecycle_transfer_started(&lc);
	frame_status = esp32s31_dvp_frame_classify(614400, 614464, 614464,
	    &payload_size);
	assert(frame_status == ESP32S31_DVP_FRAME_COMPLETE);
	assert(payload_size == 614400);
	assert(esp32s31_dvp_frame_classify(614400, 614464, 0,
	    &payload_size) == ESP32S31_DVP_FRAME_INVALID);
	assert(esp32s31_dvp_frame_classify(614400, 614464, 614465,
	    &payload_size) == ESP32S31_DVP_FRAME_INVALID);
	assert(esp32s31_dvp_frame_classify(SIZE_MAX, SIZE_MAX, SIZE_MAX,
	    &payload_size) == ESP32S31_DVP_FRAME_INVALID);
	assert(!esp32s31_dvp_lifecycle_rearm_allowed(&lc, first));
	assert(!esp32s31_dvp_lifecycle_start_allowed(&lc));

	/* Stop invalidates an already queued completion and blocks restart. */
	assert(esp32s31_dvp_lifecycle_stop_acquire(&lc));
	assert(!esp32s31_dvp_lifecycle_stop_acquire(&lc));
	esp32s31_dvp_lifecycle_begin_stop(&lc);
	assert(!esp32s31_dvp_lifecycle_completion_current(&lc, first));
	assert(!esp32s31_dvp_lifecycle_finish_stop(&lc));
	assert(!esp32s31_dvp_lifecycle_start_allowed(&lc));

	/* A later cleanup can finish after both hardware sides stop. */
	lc.dma_active = false;
	lc.sensor_streaming = false;
	assert(esp32s31_dvp_lifecycle_finish_stop(&lc));
	esp32s31_dvp_lifecycle_stop_release(&lc);
	assert(esp32s31_dvp_lifecycle_start_allowed(&lc));
	second = esp32s31_dvp_lifecycle_begin_stream(&lc);
	assert(second != first);
	assert(!esp32s31_dvp_lifecycle_completion_current(&lc, first));
	assert(esp32s31_dvp_lifecycle_completion_current(&lc, second));

	/* A sensor S_STREAM failure after arming is unwound before retry. */
	first = esp32s31_dvp_lifecycle_begin_stream(&retry);
	esp32s31_dvp_lifecycle_transfer_started(&retry);
	retry.dma_active = true;
	assert(esp32s31_dvp_lifecycle_sensor_start_allowed(&retry));
	assert(!esp32s31_dvp_lifecycle_sensor_start_result(&retry, EIO));
	assert(!retry.sensor_streaming);
	esp32s31_dvp_lifecycle_begin_stop(&retry);
	retry.dma_active = false;
	assert(esp32s31_dvp_lifecycle_finish_stop(&retry));
	assert(esp32s31_dvp_lifecycle_start_allowed(&retry));
	second = esp32s31_dvp_lifecycle_begin_stream(&retry);
	assert(second != first);
	esp32s31_dvp_lifecycle_transfer_started(&retry);
	retry.dma_active = true;
	assert(esp32s31_dvp_lifecycle_sensor_start_result(&retry, 0));
	assert(retry.sensor_streaming);
	esp32s31_dvp_lifecycle_begin_stop(&retry);
	retry.sensor_streaming = false;
	retry.dma_active = false;
	assert(esp32s31_dvp_lifecycle_finish_stop(&retry));
	assert(esp32s31_dvp_lifecycle_start_allowed(&retry));

	return 0;
}
