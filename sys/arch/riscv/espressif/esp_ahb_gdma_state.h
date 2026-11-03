/* $NetBSD$ */
/* SPDX-FileCopyrightText: 2026 The EmberBSD Project */
/* SPDX-License-Identifier: BSD-2-Clause */

#ifndef _RISCV_ESPRESSIF_ESP_AHB_GDMA_STATE_H_
#define _RISCV_ESPRESSIF_ESP_AHB_GDMA_STATE_H_

#include <sys/types.h>

static inline bool
esp_ahb_gdma_error_ready(bool done, bool stopping, int error)
{
	return done && !stopping && error != 0;
}

static inline size_t
esp_ahb_gdma_dispatch_count(bool pending, bool stopping, size_t count)
{
	return pending && !stopping ? count : 0;
}

static inline bool
esp_ahb_gdma_callbacks_drained(bool callback_running, bool result_busy,
	bool softintr_scheduled)
{
	return !callback_running && !result_busy && !softintr_scheduled;
}

static inline bool
esp_ahb_gdma_halt_required(bool running, bool done, bool desc_synced,
	bool broken)
{
	return running || done || desc_synced || broken;
}

static inline bool
esp_ahb_gdma_can_start(bool claimed, bool running, bool broken, bool stopping,
	bool callback_pending, bool callback_running, bool softintr_scheduled,
	bool result_busy, bool desc_synced)
{
	return claimed && !running && !broken && !stopping && !callback_pending &&
	    !callback_running && !softintr_scheduled && !result_busy &&
	    !desc_synced;
}

#endif
