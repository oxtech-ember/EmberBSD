/* SPDX-License-Identifier: MIT */
/*
 * Copyright (c) 2023 Imagination Technologies Ltd.
 * Copyright (c) 2026 EmberBSD contributors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Firmware initialization rules: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native CPU initializer: EmberBSD, AI-assisted. See a733-gpu-fwinit.md.
 */

#include "sun60i_a733_gpu_fwinit.h"
#include "powervr/pvr_rogue_fwif.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

#define ROUND64(n)	(((n) + 63U) & ~63U)
#define ROUND4K(n)	(((n) + 4095U) & ~4095U)
#define TRACE_BYTES	(ROGUE_FW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS * 4U)

/* Fixed first-boot layout, with sizes from the pinned FWIF declarations. */
enum fwinit_offset {
	O_KCCB_CTL = 0,
	O_KCCB_CMDS = ROUND64(O_KCCB_CTL + sizeof(struct rogue_fwif_ccb_ctl)),
	O_KCCB_RTN = ROUND64(O_KCCB_CMDS + A733_GPU_FWINIT_KCCB_COUNT *
	    sizeof(struct rogue_fwif_kccb_cmd)),
	O_FWCCB_CTL = ROUND64(O_KCCB_RTN + A733_GPU_FWINIT_KCCB_COUNT * 4U),
	O_FWCCB_CMDS = ROUND64(O_FWCCB_CTL + sizeof(struct rogue_fwif_ccb_ctl)),
	O_POWER_SYNC = ROUND64(O_FWCCB_CMDS + A733_GPU_FWINIT_FWCCB_COUNT *
	    sizeof(struct rogue_fwif_fwccb_cmd)),
	O_HWR = ROUND64(O_POWER_SYNC + 4U),
	O_MMUCACHE_SYNC = ROUND64(O_HWR + sizeof(struct rogue_fwif_hwrinfobuf)),
	O_SYSDATA = ROUND64(O_MMUCACHE_SYNC + 4U),
	O_FAULT = ROUND4K(O_SYSDATA + sizeof(struct rogue_fwif_sysdata)),
	O_GPU_UTIL = O_FAULT + A733_GPU_FWINIT_PAGE_SIZE,
	O_RUNTIME_CFG = ROUND64(O_GPU_UTIL + sizeof(struct rogue_fwif_gpu_util_fwcb)),
	O_TRACE0 = ROUND64(O_RUNTIME_CFG + sizeof(struct rogue_fwif_runtime_cfg)),
	O_TRACE1 = O_TRACE0 + TRACE_BYTES,
	O_TRACE_CTL = O_TRACE1 + TRACE_BYTES,
	O_OSDATA = ROUND64(O_TRACE_CTL + sizeof(struct rogue_fwif_tracebuf)),
	O_TOTAL = ROUND4K(O_OSDATA + sizeof(struct rogue_fwif_osdata))
};

_Static_assert(O_TOTAL <= A733_GPU_FWINIT_MAX_WORK_SIZE, "work arena size");
_Static_assert(A733_GPU_FWINIT_WORK_VA + O_TOTAL <
    A733_GPU_FWINIT_CONNECTION_VA, "firmware work/config separation");
_Static_assert(sizeof(struct rogue_fwif_connection_ctl) <= 4096,
    "connection page");
_Static_assert(sizeof(struct rogue_fwif_osinit) <= 4096, "osinit page");
_Static_assert(sizeof(struct rogue_fwif_sysinit) <= 4096, "sysinit page");
_Static_assert(MAX_THREAD_NUM == 2, "two firmware trace buffers");
_Static_assert(TRACE_BYTES % 64 == 0, "trace buffer alignment");

#define WORK_REGION(id, off, bytes) \
	[A733_GPU_FWINIT_ ## id] = { A733_GPU_FWINIT_WORK, off, bytes, \
	    A733_GPU_FWINIT_WORK_VA + (off) }
#define CONFIG_REGION(id, off, type) \
	[A733_GPU_FWINIT_ ## id] = { A733_GPU_FWINIT_CONFIG, off, \
	    sizeof(struct type), A733_GPU_FWINIT_ ## id ## _VA }

static const struct sun60i_a733_gpu_fwinit_layout fwinit_layout = {
	.total_work_size = O_TOTAL,
	.object = {
		WORK_REGION(KCCB_CTL, O_KCCB_CTL, sizeof(struct rogue_fwif_ccb_ctl)),
		WORK_REGION(KCCB_CMDS, O_KCCB_CMDS,
		    A733_GPU_FWINIT_KCCB_COUNT * sizeof(struct rogue_fwif_kccb_cmd)),
		WORK_REGION(KCCB_RTN, O_KCCB_RTN, A733_GPU_FWINIT_KCCB_COUNT * 4U),
		WORK_REGION(FWCCB_CTL, O_FWCCB_CTL, sizeof(struct rogue_fwif_ccb_ctl)),
		WORK_REGION(FWCCB_CMDS, O_FWCCB_CMDS,
		    A733_GPU_FWINIT_FWCCB_COUNT * sizeof(struct rogue_fwif_fwccb_cmd)),
		WORK_REGION(POWER_SYNC, O_POWER_SYNC, 4),
		WORK_REGION(HWR, O_HWR, sizeof(struct rogue_fwif_hwrinfobuf)),
		WORK_REGION(MMUCACHE_SYNC, O_MMUCACHE_SYNC, 4),
		WORK_REGION(SYSDATA, O_SYSDATA, sizeof(struct rogue_fwif_sysdata)),
		WORK_REGION(FAULT, O_FAULT, A733_GPU_FWINIT_PAGE_SIZE),
		WORK_REGION(GPU_UTIL, O_GPU_UTIL, sizeof(struct rogue_fwif_gpu_util_fwcb)),
		WORK_REGION(RUNTIME_CFG, O_RUNTIME_CFG,
		    sizeof(struct rogue_fwif_runtime_cfg)),
		WORK_REGION(TRACE0, O_TRACE0, TRACE_BYTES),
		WORK_REGION(TRACE1, O_TRACE1, TRACE_BYTES),
		WORK_REGION(TRACE_CTL, O_TRACE_CTL, sizeof(struct rogue_fwif_tracebuf)),
		WORK_REGION(OSDATA, O_OSDATA, sizeof(struct rogue_fwif_osdata)),
		CONFIG_REGION(CONNECTION, 0, rogue_fwif_connection_ctl),
		CONFIG_REGION(OSINIT, 4096, rogue_fwif_osinit),
		CONFIG_REGION(SYSINIT, 8192, rogue_fwif_sysinit),
	}
};

#undef WORK_REGION
#undef CONFIG_REGION

static void
fwinit_zero(void *p, size_t size)
{
	uint8_t *bytes = p;
	size_t i;

	for (i = 0; i < size; i++)
		bytes[i] = 0;
}

static int
fwinit_span_valid(const void *p, size_t size, size_t alignment)
{
	uintptr_t start = (uintptr_t)p;

	return p != NULL && size != 0 && start % alignment == 0 &&
	    size - 1 <= (uintptr_t)-1 - start;
}

static int
fwinit_overlap(const void *p, size_t size, const void *q, size_t qsize)
{
	uintptr_t a = (uintptr_t)p, b = (uintptr_t)q;

	return a <= b ? b - a < size : a - b < qsize;
}

size_t
sun60i_a733_gpu_fwinit_work_size(void)
{
	return O_TOTAL;
}

int
sun60i_a733_gpu_fwinit(void *work, size_t work_size, void *config,
    size_t config_size, uint64_t work_phys, uint64_t core_hz,
    uint32_t clusters, uint32_t slc_kib,
    struct sun60i_a733_gpu_fwinit_layout *out)
{
	uint8_t *bytes = work, *cfg = config;
	struct rogue_fwif_ccb_ctl *kccb, *fwccb;
	struct rogue_fwif_osinit *osinit;
	struct rogue_fwif_sysinit *sysinit;
	struct rogue_fwif_sysdata *sysdata;
	struct rogue_fwif_osdata *osdata;
	struct rogue_fwif_runtime_cfg *runtime;
	struct rogue_fwif_gpu_util_fwcb *util;
	struct rogue_fwif_tracebuf *trace;
	uint32_t *fault;
	size_t i;

	if (work_size < O_TOTAL || work_size > A733_GPU_FWINIT_MAX_WORK_SIZE ||
	    work_size % 4096 != 0 || config_size != A733_GPU_FWINIT_CONFIG_SIZE ||
	    core_hz == 0 || core_hz > UINT32_MAX || clusters == 0 || slc_kib == 0 ||
	    work_phys % 4096 != 0 || work_phys >= (UINT64_C(1) << 36) ||
	    work_size > (UINT64_C(1) << 36) - work_phys ||
	    !fwinit_span_valid(work, work_size, 4096) ||
	    !fwinit_span_valid(config, config_size, 4096) ||
	    !fwinit_span_valid(out, sizeof(*out),
	    _Alignof(struct sun60i_a733_gpu_fwinit_layout)) ||
	    fwinit_overlap(work, work_size, config, config_size) ||
	    fwinit_overlap(work, work_size, out, sizeof(*out)) ||
	    fwinit_overlap(config, config_size, out, sizeof(*out)))
		return EINVAL;
	if (work_phys < UINT64_C(0x1fc04000) &&
	    work_phys + work_size > UINT64_C(0x1fc00000))
		return EOPNOTSUPP;

	/* All failure paths end above. No device may access these buffers yet. */
	fwinit_zero(work, O_TOTAL);
	fwinit_zero(config, config_size);
	kccb = (void *)(bytes + O_KCCB_CTL);
	fwccb = (void *)(bytes + O_FWCCB_CTL);
	kccb->wrap_mask = A733_GPU_FWINIT_KCCB_COUNT - 1;
	kccb->cmd_size = sizeof(struct rogue_fwif_kccb_cmd);
	fwccb->wrap_mask = A733_GPU_FWINIT_FWCCB_COUNT - 1;
	fwccb->cmd_size = sizeof(struct rogue_fwif_fwccb_cmd);

	osinit = (void *)(cfg + 4096);
	osinit->kernel_ccbctl_fw_addr = A733_GPU_FWINIT_WORK_VA + O_KCCB_CTL;
	osinit->kernel_ccb_fw_addr = A733_GPU_FWINIT_WORK_VA + O_KCCB_CMDS;
	osinit->kernel_ccb_rtn_slots_fw_addr = A733_GPU_FWINIT_WORK_VA + O_KCCB_RTN;
	osinit->firmware_ccbctl_fw_addr = A733_GPU_FWINIT_WORK_VA + O_FWCCB_CTL;
	osinit->firmware_ccb_fw_addr = A733_GPU_FWINIT_WORK_VA + O_FWCCB_CMDS;
	osinit->rogue_fwif_hwr_info_buf_ctl_fw_addr = A733_GPU_FWINIT_WORK_VA + O_HWR;
	osinit->fw_os_data_fw_addr = A733_GPU_FWINIT_WORK_VA + O_OSDATA;
	rogue_fwif_compchecks_bvnc_init(&osinit->rogue_comp_checks.hw_bvnc);
	rogue_fwif_compchecks_bvnc_init(&osinit->rogue_comp_checks.fw_bvnc);

	osdata = (void *)(bytes + O_OSDATA);
	osdata->power_sync_fw_addr = A733_GPU_FWINIT_WORK_VA + O_POWER_SYNC;
	sysdata = (void *)(bytes + O_SYSDATA);
	if (slc_kib < 4)
		sysdata->config_flags = ROGUE_FWIF_INICFG_DISABLE_DM_OVERLAP;
	fault = (void *)(bytes + O_FAULT);
	for (i = 0; i < 4096 / sizeof(*fault); i++)
		fault[i] = UINT32_C(0xdeadbee0);

	runtime = (void *)(bytes + O_RUNTIME_CFG);
	runtime->core_clock_speed = (uint32_t)core_hz;
	runtime->default_dusts_num_init = clusters;
	runtime->active_pm_latency_persistant = true;
	util = (void *)(bytes + O_GPU_UTIL);
	util->last_word = PVR_FWIF_GPU_UTIL_STATE_IDLE;

	trace = (void *)(bytes + O_TRACE_CTL);
	trace->tracebuf_size_in_dwords = ROGUE_FW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS;
	for (i = 0; i < MAX_THREAD_NUM; i++) {
		trace->tracebuf[i].trace_buffer_fw_addr =
		    A733_GPU_FWINIT_WORK_VA + O_TRACE0 + i * TRACE_BYTES;
		/* Upstream marks this pointer for host readers, not firmware. */
		trace->tracebuf[i].trace_buffer = NULL;
	}

	sysinit = (void *)(cfg + 8192);
	sysinit->fault_phys_addr = work_phys + O_FAULT;
	sysinit->pds_exec_base = UINT64_C(0xda00000000);
	sysinit->usc_exec_base = UINT64_C(0xe000000000);
	sysinit->runtime_cfg_fw_addr = A733_GPU_FWINIT_WORK_VA + O_RUNTIME_CFG;
	sysinit->trace_buf_ctl_fw_addr = A733_GPU_FWINIT_WORK_VA + O_TRACE_CTL;
	sysinit->fw_sys_data_fw_addr = A733_GPU_FWINIT_WORK_VA + O_SYSDATA;
	sysinit->gpu_util_fw_cb_ctl_fw_addr = A733_GPU_FWINIT_WORK_VA + O_GPU_UTIL;
	sysinit->initial_core_clock_speed = (uint32_t)core_hz;
	sysinit->firmware_perf = FW_PERF_CONF_NONE;
	sysinit->gpio_validation_mode = ROGUE_FWIF_GPIO_VAL_OFF;
	sysinit->marker_val = 1;
	*out = fwinit_layout;
	return 0;
}
