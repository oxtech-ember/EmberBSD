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
 * KCCB publication and completion reference: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native one-command probe: EmberBSD, AI-assisted. See a733-gpu-fwprobe.md.
 */

#ifndef _SUN60I_A733_GPU_FWPROBE_H
#define _SUN60I_A733_GPU_FWPROBE_H

#include <sys/types.h>
#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_gpu_fwboot.h>

#define A733_GPU_FWPROBE_TIMEOUT_US	1000000
#define A733_GPU_FWPROBE_POLL_US		100
#define A733_GPU_FWPROBE_MAX_POLLS	10001

struct sun60i_a733_gpu_fwprobe_snapshot {
	uint32_t read_offset, write_offset, wrap_mask, cmd_size, rtn;
	uint32_t fw_read_offset, fw_write_offset, fw_wrap_mask, fw_cmd_size;
	uint32_t fw_command, cmds_executed, pow_state, hwr_state, fw_faults;
	uint32_t log_type, fw_state, os_state, alive_fw_token, alive_os_token;
	uint8_t firmware_started, compchecks_updated, os_count_support;
	uint32_t hw_layout_version, fw_layout_version;
	uint64_t hw_bvnc, fw_bvnc;
	uint32_t fw_processor_version, ddk_version, ddk_build, build_options;
};

struct sun60i_a733_gpu_fwprobe_result {
	bool attempted, published, kick_attempted, kicked;
	int error;
	uint32_t slot, polls;
	struct sun60i_a733_gpu_fwprobe_snapshot before, after;
};

/*
 * One LOGTYPE_UPDATE command in initial KCCB slot 0. This proves command
 * execution only. It does not start a graphics context or an OS handshake.
 * Supply the unchanged prepared owner and its successful fwboot result.
 * Work/config mappings must already be verified coherent and not bouncing.
 * The caller holds an adaptive mutex at IPL_NONE, keeps power/clocks on,
 * maps all A733_GPU_FWSTART_REG_SIZE registers, and excludes other queue,
 * DMA, IRQ, reset, suspend, and reclamation users for the whole operation.
 *
 * Zero result once and keep it with this boot's owner. Never reset it or
 * substitute another result to retry. Repeated calls return EBUSY unchanged.
 * Structural errors preserve result and perform no I/O. attempted is set
 * before read-only runtime checks; these failures also consume the attempt.
 * published is set before the write-offset store. kick_attempted precedes
 * MMIO; kicked records a successful write and ordering barrier only.
 *
 * CPU writes are limited to command 0, return word 0, and KCCB write_offset.
 * Every other device-owned byte is read-only, including connection/FWCCB.
 * No memory is freed, unmapped, reinitialized, or returned to CPU ownership.
 * Fault, timeout, and success all retain the previous boot's DMA ownership.
 * Poll at most one monotonic second and 10001 times, with delays <=100 us.
 * Scheduling can extend return time. Snapshots are non-atomic diagnostics.
 * Return positive errno and cache attempted-operation errors in result.
 */
int sun60i_a733_gpu_fwprobe(bus_space_tag_t, bus_space_handle_t,
    struct sun60i_a733_gpu_fwmem *, const struct sun60i_a733_gpu_fwboot_result *,
    struct sun60i_a733_gpu_fwprobe_result *);

#endif /* _SUN60I_A733_GPU_FWPROBE_H */
