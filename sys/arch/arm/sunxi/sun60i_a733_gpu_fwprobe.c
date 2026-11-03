/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD; strict A733 adapter for the common PowerVR queue. */
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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <arm/sunxi/sun60i_a733_gpu_fwprobe.h>

static int
a733_probe_owner(const struct pvr_fwmem *mem)
{
	if (mem != NULL && ((uintptr_t)mem % _Alignof(struct pvr_fwmem) != 0 ||
	    sizeof(*mem) - 1 > (uintptr_t)-1 - (uintptr_t)mem))
		return EINVAL;
	return mem != NULL && mem->profile != PVR_FW_PROFILE_A733 ? EOPNOTSUPP : 0;
}

int
sun60i_a733_gpu_fwprobe(bus_space_tag_t t, bus_space_handle_t h,
    struct pvr_fwmem *mem, const struct pvr_fwboot_result *boot,
    struct pvr_fwprobe_result *out)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwprobe(t, h, mem, boot, out);
}

int
sun60i_a733_gpu_fwprobe_mmucache(bus_space_tag_t t, bus_space_handle_t h,
    struct pvr_fwmem *mem, const struct pvr_fwboot_result *boot,
    const struct pvr_fwprobe_result *prev, struct pvr_fwprobe_result *out)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwprobe_mmucache(t, h, mem, boot, prev, out);
}

int
sun60i_a733_gpu_fwprobe_cmd(bus_space_tag_t t, bus_space_handle_t h,
    struct pvr_fwmem *mem, const struct pvr_fwboot_result *boot,
    const struct pvr_fwprobe_result *prev, const struct rogue_fwif_kccb_cmd *cmd,
    unsigned int slot, uint32_t log, struct pvr_fwprobe_result *out)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwprobe_cmd(t, h, mem, boot, prev, cmd, slot, log, out);
}

int
sun60i_a733_gpu_fwprobe_observe(struct pvr_fwmem *mem,
    const struct pvr_fwboot_result *boot, unsigned int slot, uint32_t cmd,
    uint32_t log, struct pvr_fwprobe_observation *out)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwprobe_observe(mem, boot, slot, cmd, log, out);
}

int
sun60i_a733_gpu_fwqueue_attach(struct pvr_fwmem *mem,
    const struct pvr_fwboot_result *boot, const struct pvr_fwprobe_result *prev,
    struct pvr_fwqueue *q)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwqueue_attach(mem, boot, prev, q);
}

int
sun60i_a733_gpu_fwqueue_cmd(bus_space_tag_t t, bus_space_handle_t h,
    struct pvr_fwmem *mem, const struct pvr_fwboot_result *boot,
    struct pvr_fwqueue *q, const struct rogue_fwif_kccb_cmd *cmd,
    uint32_t log, struct pvr_fwprobe_result *out)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwqueue_cmd(t, h, mem, boot, q, cmd, log, out);
}

int
sun60i_a733_gpu_fwqueue_observe(struct pvr_fwmem *mem,
    const struct pvr_fwboot_result *boot, struct pvr_fwqueue *q,
    struct pvr_fwprobe_observation *out)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwqueue_observe(mem, boot, q, out);
}

int
sun60i_a733_gpu_fwqueue_event(struct pvr_fwmem *mem,
    const struct pvr_fwboot_result *boot, struct pvr_fwqueue *q, uint32_t *id)
{
	int error = a733_probe_owner(mem);
	return error != 0 ? error : pvr_fwqueue_event(mem, boot, q, id);
}
