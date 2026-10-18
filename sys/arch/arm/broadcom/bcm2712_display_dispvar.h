/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _ARM_BROADCOM_BCM2712_DISPLAY_DISPVAR_H_
#define _ARM_BROADCOM_BCM2712_DISPLAY_DISPVAR_H_
/* Private module lifetime hooks, not a kernel API. Serialize these hooks.
 * Initialize before any DISP API caller. Quiesce callers before fini.
 * Fini refuses a live/faulted owner. Clean unload must destroy the mutex.
 */
int bcm2712_disp_adapter_init(void);
int bcm2712_disp_adapter_fini(void);
#endif
