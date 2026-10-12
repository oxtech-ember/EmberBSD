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
 * Firmware boot wait reference: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native DMA handoff: EmberBSD, AI-assisted. See a733-gpu-fwboot.md.
 */

#ifndef _SUN60I_A733_GPU_FWBOOT_H
#define _SUN60I_A733_GPU_FWBOOT_H

#include <sys/types.h>
#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_gpu_fwmem.h>
#include <arm/sunxi/sun60i_a733_gpu_fwstart.h>

#define A733_GPU_FWBOOT_TIMEOUT_US	5000000
#define A733_GPU_FWBOOT_POLL_US		1000
#define A733_GPU_FWBOOT_MAX_POLLS	5001

struct sun60i_a733_gpu_fwboot_result {
	/* Persistent one-shot guard and DMA ownership. Never clear to retry. */
	bool attempted, device_owned;
	int error;
	struct sun60i_a733_gpu_fwstart_result start;
	uint32_t polls;
	/* Cached, non-atomic observation. Reading this result performs no I/O. */
	bool snapshot_valid;
	uint8_t firmware_started, compchecks_updated, os_count_support;
	uint32_t firmware_started_timestamp;
	uint32_t fw_state, os_state, alive_fw_token, alive_os_token;
	uint32_t hw_layout_version, fw_layout_version;
	uint64_t hw_bvnc, fw_bvnc;
	uint32_t fw_processor_version, ddk_version, ddk_build, build_options;
};

/*
 * Diagnostic handoff and bounded firmware boot wait. The caller holds an
 * adaptive mutex at IPL_NONE for the whole operation, including the wait.
 * Fresh hardware readiness and a full 64 KiB register mapping are caller
 * prerequisites. Supply the unchanged output of fwmem_prepare/interface.
 * No device may already own these buffers. No IRQ handler may access them.
 *
 * Zero-initialize result once and retain it for this memory owner's lifetime.
 * Do not reset or replace the result to retry. attempted is set before DMA
 * synchronization and MMIO. Any repeated attempt returns EBUSY unchanged.
 * Preflight errors leave result and all buffers unchanged, with no I/O.
 *
 * All DMA buffers are synchronized before fwstart. Untouched fwstart failure
 * pairs POST sync and returns CPU ownership. Any possible hardware touch
 * retains device ownership, including a faulting reset-release write.
 * Success, timeout, and malformed firmware_started also retain ownership.
 * Never free/unmap the buffers while device_owned is true. Further writes
 * require a separately validated shared-memory protocol, such as fwprobe.
 * This helper never frees, unmaps, or writes firmware buffers after handoff.
 *
 * Poll config through POSTREAD/read-only observation/PREREAD cycles. Accept
 * only raw firmware_started byte 0 or 1; another value returns EPROTO.
 * Wait at most a five-second monotonic interval and 5001 observations.
 * Busy delays are at most 1000 us each; scheduling can extend return time.
 * Return positive errno and cache attempted-operation errors in result.
 * Firmware boot is not command-queue or GPU acceleration acceptance.
 */
int sun60i_a733_gpu_fwboot(bus_space_tag_t, bus_space_handle_t,
    struct sun60i_a733_gpu_fwmem *, struct sun60i_a733_gpu_fwboot_result *);

#endif /* _SUN60I_A733_GPU_FWBOOT_H */
