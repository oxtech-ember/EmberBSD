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
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>

#define A733_GPU_FWPROBE_LOGTYPE		UINT32_C(0x2abc00ce)
#define A733_GPU_FWPROBE_MMUCACHE		UINT32_C(0x2abc0066)
#define A733_GPU_FWPROBE_TRACE_LOGTYPE	UINT32_C(0x0000003f)

#define A733_GPU_FWPROBE_TIMEOUT_US	1000000
#define A733_GPU_FWPROBE_POLL_US		100
#define A733_GPU_FWPROBE_MAX_POLLS	10001

struct sun60i_a733_gpu_fwprobe_snapshot {
	uint32_t read_offset, write_offset, wrap_mask, cmd_size, rtn;
	uint32_t fw_read_offset, fw_write_offset, fw_wrap_mask, fw_cmd_size;
	uint32_t fw_command, cmds_executed, pow_state, hwr_state, fw_faults;
	uint32_t log_type, fw_state, os_state, alive_fw_token, alive_os_token;
	uint32_t mmucache_sync;
	uint8_t firmware_started, compchecks_updated, os_count_support;
	uint32_t hw_layout_version, fw_layout_version;
	uint64_t hw_bvnc, fw_bvnc;
	uint32_t fw_processor_version, ddk_version, ddk_build, build_options;
};

struct sun60i_a733_gpu_fwprobe_result {
	bool attempted, published, kick_attempted, kicked;
	int error;
	uint32_t slot, polls;
	uint32_t command, cache_flags, sync_fw_addr, sync_update_value;
	struct sun60i_a733_gpu_fwprobe_snapshot before, after;
};

struct sun60i_a733_gpu_fwprobe_observation {
	bool valid;
	int error;
	struct sun60i_a733_gpu_fwprobe_snapshot state;
};

/*
 * Read current firmware state after a previously accepted slot. Revalidate
 * the exact DMA layout and boot ownership before every observation. Require
 * the queue to remain drained at slot + 1. Supply the accepted command kind.
 * LOGTYPE/MMUCACHE require return word 1. KICK has no return-slot completion;
 * its consumption only permits the caller to check CCCB/UFO completion.
 * Use the same caller exclusion and DMA lifetime as command submission.
 * Never publish, consume a queue, write MMIO, or modify acceptance metadata.
 * Structural errors leave *out unchanged and perform no I/O. Otherwise set
 * valid, retain the snapshot even on a runtime error, and return that error.
 */
int sun60i_a733_gpu_fwprobe_observe(struct sun60i_a733_gpu_fwmem *,
    const struct sun60i_a733_gpu_fwboot_result *, unsigned int, uint32_t, uint32_t,
    struct sun60i_a733_gpu_fwprobe_observation *);

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

/*
 * One MMUCACHE command in slot 1, after the unchanged successful LOGTYPE
 * result from the same owner and boot. Keep a separate persistent result.
 * Require current read/write offsets 1. Publish write offset 2 and require
 * return word 1 == CMD_EXECUTED and read/write offsets 2 for success.
 * Use the existing MMUCACHE_SYNC object, exact flags 0x0400001f, and value 0.
 * The sync value is not a completion token. No page table or context changes.
 * All lifetime, coherence, exclusion, timeout, and retention rules above apply.
 * CPU writes are limited to command 1, return word 1, and KCCB write_offset.
 * command and payload fields identify the operation in the cached result.
 */
int sun60i_a733_gpu_fwprobe_mmucache(bus_space_tag_t, bus_space_handle_t,
    struct sun60i_a733_gpu_fwmem *, const struct sun60i_a733_gpu_fwboot_result *,
    const struct sun60i_a733_gpu_fwprobe_result *,
    struct sun60i_a733_gpu_fwprobe_result *);

/*
 * Publish one caller-prepared KCCB command in the slot after the given
 * fully successful predecessor result. Allowed types are LOGTYPE_UPDATE,
 * a repeated MMUCACHE sync, or one compute KICK. All lifetime, coherence,
 * exclusion, timeout, and retention rules of the probes above apply unchanged.
 * Supply the exact expected shared log type for every runtime observation.
 * Initial probes always require zero. Later tracing requires TRACE and at
 * least one valid group. Only LOGTYPE_UPDATE can change the previous mode.
 * The caller updates the shared log type before LOGTYPE_UPDATE publication.
 * The command payload is copied before any shared-memory write.
 * KICK succeeds here when firmware consumes its KCCB entry. It does not
 * require a return word. CCCB, UFO, and fresh fault checks remain separate.
 */
int sun60i_a733_gpu_fwprobe_cmd(bus_space_tag_t, bus_space_handle_t,
    struct sun60i_a733_gpu_fwmem *, const struct sun60i_a733_gpu_fwboot_result *,
    const struct sun60i_a733_gpu_fwprobe_result *,
    const struct rogue_fwif_kccb_cmd *, unsigned int, uint32_t,
    struct sun60i_a733_gpu_fwprobe_result *);

#endif /* _SUN60I_A733_GPU_FWPROBE_H */
