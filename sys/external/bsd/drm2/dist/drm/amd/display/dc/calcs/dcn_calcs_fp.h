/*	$NetBSD$	*/

/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 * Copyright 2019 Raptor Engineering, LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * FP/glue separation by EmberBSD contributors (AI-assisted).
 * Derived from amdgpu_dcn_calcs.c, NetBSD revision 1.4.
 *
 * Authors: AMD
 *
 */

#ifndef DCN_CALCS_FP_H
#define DCN_CALCS_FP_H

#include <linux/types.h>

struct dc;
struct dc_state;
struct dc_clocks;
struct dm_pp_clock_levels_with_voltage;

/* Caller must hold the FP context; these routines must not be inlined. */
bool dcn_bw_apply_registry_override_fp(struct dc *)
    __attribute__((__noinline__));
bool dcn_validate_bandwidth_fp(struct dc *, struct dc_state *, bool,
    unsigned long long *, unsigned long long *) __attribute__((__noinline__));
unsigned int dcn_find_dcfclk_suits_all_fp(const struct dc *, struct dc_clocks *)
    __attribute__((__noinline__));
void dcn_bw_update_fclk_fp(struct dc *, struct dm_pp_clock_levels_with_voltage *,
    bool) __attribute__((__noinline__));
void dcn_bw_update_dcfclk_fp(struct dc *, struct dm_pp_clock_levels_with_voltage *,
    bool) __attribute__((__noinline__));
void dcn_bw_get_wm_clocks_fp(struct dc *, volatile int *, volatile int *,
    volatile int *) __attribute__((__noinline__));
void dcn_bw_sync_calcs_and_dml_fp(struct dc *) __attribute__((__noinline__));

#endif /* DCN_CALCS_FP_H */
