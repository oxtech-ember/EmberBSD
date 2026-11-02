/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "../esp32s31_dvp_lifecycle.h"

int
main(void)
{
	struct esp32s31_dvp_lifecycle lc = { 0 };
	uint64_t first, second;

	assert(esp32s31_dvp_lifecycle_start_allowed(&lc));
	first = esp32s31_dvp_lifecycle_begin_stream(&lc);
	lc.sensor_streaming = true;
	lc.dma_active = true;
	assert(esp32s31_dvp_lifecycle_completion_current(&lc, first));
	assert(!esp32s31_dvp_lifecycle_rearm_allowed(&lc, first));
	assert(esp32s31_dvp_lifecycle_result_ready(&lc, first));
	assert(!esp32s31_dvp_lifecycle_rearm_allowed(&lc, first));
	assert(esp32s31_dvp_lifecycle_dma_quiesced(&lc, first));
	assert(esp32s31_dvp_lifecycle_rearm_allowed(&lc, first));
	esp32s31_dvp_lifecycle_transfer_started(&lc);
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

	return 0;
}
