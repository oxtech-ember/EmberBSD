/*	$NetBSD: amdgpu_dcn_calcs.c,v 1.4 2023/07/20 21:48:49 riastradh Exp $	*/

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
 * Authors: AMD
 *
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: amdgpu_dcn_calcs.c,v 1.4 2023/07/20 21:48:49 riastradh Exp $");

#include "dm_services.h"
#include "dc.h"
#include "dcn_calcs.h"
#include "dcn_calcs_fp.h"
#include "dal_asic_id.h"
#include "resource.h"

#define DC_LOGGER dc->ctx->logger

#define WM_SET_COUNT 4
#define WM_A 0
#define WM_B 1
#define WM_C 2
#define WM_D 3

/*
 * Compile these entry points with normal kernel flags.  Even integer-only
 * initialization and structure copies may use FP registers in an FP-enabled
 * translation unit, before DC_FP_START() can save the interrupted context.
 * The noinline FP routines use pointer/integer arguments and run only inside
 * these scopes; PPLIB callbacks retain their original position outside them.
 */
static bool dcn_bw_apply_registry_override(struct dc *dc)
{
	bool updated;

	DC_FP_START();
	updated = dcn_bw_apply_registry_override_fp(dc);
	DC_FP_END();
	return updated;
}

unsigned int get_highest_allowed_voltage_level(uint32_t hw_internal_rev)
{
	/* for dali & pollock, the highest voltage level we want is 0 */
	if (ASICREV_IS_POLLOCK(hw_internal_rev) || ASICREV_IS_DALI(hw_internal_rev))
		return 0;

	/* we are ok with all levels */
	return 4;
}

bool dcn_validate_bandwidth(
		struct dc *dc,
		struct dc_state *context,
		bool fast_validate)
{
	/*
	 * we want a breakdown of the various stages of validation, which the
	 * perf_trace macro doesn't support
	 */
	BW_VAL_TRACE_SETUP();

	struct dcn_bw_internal_vars *v = &context->dcn_bw_vars;
	volatile bool bw_limit_pass;

	PERFORMANCE_TRACE_START();

	BW_VAL_TRACE_COUNT();

	if (dcn_bw_apply_registry_override(dc))
		dcn_bw_sync_calcs_and_dml(dc);

	memset(v, 0, sizeof(*v));
	DC_FP_START();
	bw_limit_pass = dcn_validate_bandwidth_fp(dc, context, fast_validate,
	    &voltage_level_tick, &watermark_tick);
	DC_FP_END();

	PERFORMANCE_TRACE_END();
	BW_VAL_TRACE_FINISH();

	if (bw_limit_pass && v->voltage_level <= get_highest_allowed_voltage_level(dc->ctx->asic_id.hw_internal_rev))
		return true;
	else
		return false;
}

unsigned int dcn_find_dcfclk_suits_all(const struct dc *dc,
		struct dc_clocks *clocks)
{
	unsigned int clock;

	DC_FP_START();
	clock = dcn_find_dcfclk_suits_all_fp(dc, clocks);
	DC_FP_END();
	return clock;
}

void dcn_bw_update_from_pplib(struct dc *dc)
{
	struct dc_context *ctx = dc->ctx;
	struct dm_pp_clock_levels_with_voltage fclks = {0}, dcfclks = {0};
	bool res;

	/* TODO: This is not the proper way to obtain fabric_and_dram_bandwidth, should be min(fclk, memclk) */
	res = dm_pp_get_clock_levels_by_type_with_voltage(
			ctx, DM_PP_CLOCK_TYPE_FCLK, &fclks);

	DC_FP_START();
	dcn_bw_update_fclk_fp(dc, &fclks, res);
	DC_FP_END();

	res = dm_pp_get_clock_levels_by_type_with_voltage(
			ctx, DM_PP_CLOCK_TYPE_DCFCLK, &dcfclks);

	DC_FP_START();
	dcn_bw_update_dcfclk_fp(dc, &dcfclks, res);
	DC_FP_END();
}

void dcn_bw_notify_pplib_of_wm_ranges(struct dc *dc)
{
	struct pp_smu_funcs_rv *pp = NULL;
	struct pp_smu_wm_range_sets ranges = {0};
	volatile int min_fclk_khz, min_dcfclk_khz, socclk_khz;
	const int overdrive = 5000000; /* 5 GHz to cover Overdrive */

	if (dc->res_pool->pp_smu)
		pp = &dc->res_pool->pp_smu->rv_funcs;
	if (!pp || !pp->set_wm_ranges)
		return;

	DC_FP_START();
	dcn_bw_get_wm_clocks_fp(dc, &min_fclk_khz, &min_dcfclk_khz,
	    &socclk_khz);
	DC_FP_END();

	/* Now notify PPLib/SMU about which Watermarks sets they should select
	 * depending on DPM state they are in. And update BW MGR GFX Engine and
	 * Memory clock member variables for Watermarks calculations for each
	 * Watermark Set. Only one watermark set for dcn1 due to hw bug DEGVIDCN10-254.
	 */
	/* SOCCLK does not affect anytihng but writeback for DCN so for now we dont
	 * care what the value is, hence min to overdrive level
	 */
	ranges.num_reader_wm_sets = WM_SET_COUNT;
	ranges.num_writer_wm_sets = WM_SET_COUNT;
	ranges.reader_wm_sets[0].wm_inst = WM_A;
	ranges.reader_wm_sets[0].min_drain_clk_mhz = min_dcfclk_khz / 1000;
	ranges.reader_wm_sets[0].max_drain_clk_mhz = overdrive / 1000;
	ranges.reader_wm_sets[0].min_fill_clk_mhz = min_fclk_khz / 1000;
	ranges.reader_wm_sets[0].max_fill_clk_mhz = overdrive / 1000;
	ranges.writer_wm_sets[0].wm_inst = WM_A;
	ranges.writer_wm_sets[0].min_fill_clk_mhz = socclk_khz / 1000;
	ranges.writer_wm_sets[0].max_fill_clk_mhz = overdrive / 1000;
	ranges.writer_wm_sets[0].min_drain_clk_mhz = min_fclk_khz / 1000;
	ranges.writer_wm_sets[0].max_drain_clk_mhz = overdrive / 1000;

	if (dc->debug.pplib_wm_report_mode == WM_REPORT_OVERRIDE) {
		ranges.reader_wm_sets[0].wm_inst = WM_A;
		ranges.reader_wm_sets[0].min_drain_clk_mhz = 300;
		ranges.reader_wm_sets[0].max_drain_clk_mhz = 5000;
		ranges.reader_wm_sets[0].min_fill_clk_mhz = 800;
		ranges.reader_wm_sets[0].max_fill_clk_mhz = 5000;
		ranges.writer_wm_sets[0].wm_inst = WM_A;
		ranges.writer_wm_sets[0].min_fill_clk_mhz = 200;
		ranges.writer_wm_sets[0].max_fill_clk_mhz = 5000;
		ranges.writer_wm_sets[0].min_drain_clk_mhz = 800;
		ranges.writer_wm_sets[0].max_drain_clk_mhz = 5000;
	}

	ranges.reader_wm_sets[1] = ranges.writer_wm_sets[0];
	ranges.reader_wm_sets[1].wm_inst = WM_B;

	ranges.reader_wm_sets[2] = ranges.writer_wm_sets[0];
	ranges.reader_wm_sets[2].wm_inst = WM_C;

	ranges.reader_wm_sets[3] = ranges.writer_wm_sets[0];
	ranges.reader_wm_sets[3].wm_inst = WM_D;

	/* Notify PP Lib/SMU which Watermarks to use for which clock ranges */
	pp->set_wm_ranges(&pp->pp_smu, &ranges);
}

void dcn_bw_sync_calcs_and_dml(struct dc *dc)
{
	DC_FP_START();
	dcn_bw_sync_calcs_and_dml_fp(dc);
	DC_FP_END();
}
