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

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <arm/sunxi/sun60i_a733_gpu_fwprobe.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>

/* Exact pinned upstream MTS register. DM_GP is zero. */
#define A733_MTS_SCHEDULE	0x0b00
#define A733_EXPECTED_BVNC	UINT64_C(0x00240038006800b7)
#define A733_HWR_BAD	(ROGUE_FWIF_HWR_RESET_IN_PROGRESS | \
    ROGUE_FWIF_HWR_GENERAL_LOCKUP | ROGUE_FWIF_HWR_DM_STALLING | \
    ROGUE_FWIF_HWR_FW_FAULT | ROGUE_FWIF_HWR_RESTART_REQUESTED)

static bool
fwprobe_span(const void *p, size_t size, size_t alignment)
{
	uintptr_t a = (uintptr_t)p;

	return p != NULL && size != 0 && a % alignment == 0 &&
	    size - 1 <= (uintptr_t)-1 - a;
}

static bool
fwprobe_overlap(const void *p, size_t size, const void *q, size_t qsize)
{
	uintptr_t a = (uintptr_t)p, b = (uintptr_t)q;

	return a <= b ? b - a < size : a - b < qsize;
}

static bool
fwprobe_dma_valid(const struct sun60i_a733_gpu_dma *dma)
{
	return dma->loaded && dma->map != NULL && dma->nsegments == 1 &&
	    dma->size != 0 && dma->size <= A733_GPU_FWINIT_MAX_WORK_SIZE &&
	    dma->size % 4096 == 0 && dma->address % 4096 == 0 &&
	    dma->address <= (UINT64_C(1) << 36) - dma->size &&
	    (dma->address >= UINT64_C(0x1fc04000) ||
	    dma->address + dma->size <= UINT64_C(0x1fc00000)) &&
	    fwprobe_span(dma->cpu, dma->size, 4096) &&
	    dma->map->dm_mapsize == dma->size && dma->map->dm_nsegs == 1 &&
	    dma->map->dm_segs[0].ds_addr == dma->address &&
	    dma->map->dm_segs[0].ds_len == dma->size;
}

static bool
fwprobe_layout_valid(const struct sun60i_a733_gpu_fwmem *mem)
{
	static const size_t sizes[] = {
		sizeof(struct rogue_fwif_ccb_ctl),
		A733_GPU_FWINIT_KCCB_COUNT * sizeof(struct rogue_fwif_kccb_cmd),
		A733_GPU_FWINIT_KCCB_COUNT * sizeof(uint32_t),
		sizeof(struct rogue_fwif_ccb_ctl),
		A733_GPU_FWINIT_FWCCB_COUNT * sizeof(struct rogue_fwif_fwccb_cmd),
		sizeof(uint32_t), sizeof(struct rogue_fwif_hwrinfobuf),
		sizeof(uint32_t), sizeof(struct rogue_fwif_sysdata), 4096,
		sizeof(struct rogue_fwif_gpu_util_fwcb),
		sizeof(struct rogue_fwif_runtime_cfg),
		ROGUE_FW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS * sizeof(uint32_t),
		ROGUE_FW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS * sizeof(uint32_t),
		sizeof(struct rogue_fwif_tracebuf), sizeof(struct rogue_fwif_osdata),
		sizeof(struct rogue_fwif_connection_ctl),
		sizeof(struct rogue_fwif_osinit), sizeof(struct rogue_fwif_sysinit)
	};
	static const uint32_t config_va[] = {
		A733_GPU_FWINIT_CONNECTION_VA, A733_GPU_FWINIT_OSINIT_VA,
		A733_GPU_FWINIT_SYSINIT_VA
	};
	const struct sun60i_a733_gpu_fwinit_layout *layout = &mem->interface_layout;
	size_t offset = 0, i;

	__CTASSERT(__arraycount(sizes) == A733_GPU_FWINIT_OBJECT_COUNT);
	if (layout->total_work_size != sun60i_a733_gpu_fwinit_work_size() ||
	    layout->total_work_size > mem->work.size ||
	    mem->config.size != A733_GPU_FWINIT_CONFIG_SIZE)
		return false;
	for (i = 0; i < A733_GPU_FWINIT_CONNECTION; i++) {
		const struct sun60i_a733_gpu_fwinit_region *r = &layout->object[i];

		offset = roundup(offset, i == A733_GPU_FWINIT_FAULT ? 4096 : 64);
		if (r->buffer != A733_GPU_FWINIT_WORK || r->offset != offset ||
		    r->size != sizes[i] ||
		    r->fw_addr != A733_GPU_FWINIT_WORK_VA + offset ||
		    offset > mem->work.size || sizes[i] > mem->work.size - offset)
			return false;
		offset += sizes[i];
	}
	if (roundup(offset, 4096) != layout->total_work_size)
		return false;
	for (i = 0; i < __arraycount(config_va); i++) {
		const struct sun60i_a733_gpu_fwinit_region *r =
		    &layout->object[A733_GPU_FWINIT_CONNECTION + i];

		if (r->buffer != A733_GPU_FWINIT_CONFIG || r->offset != i * 4096 ||
		    r->size != sizes[A733_GPU_FWINIT_CONNECTION + i] ||
		    r->size > 4096 || r->fw_addr != config_va[i])
			return false;
	}
	return true;
}

static int
fwprobe_validate(const struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *out)
{
	const struct sun60i_a733_gpu_dma *dma[5];
	size_t i, j;

	if (!fwprobe_span(mem, sizeof(*mem), _Alignof(struct sun60i_a733_gpu_fwmem)) ||
	    !fwprobe_span(boot, sizeof(*boot),
	    _Alignof(struct sun60i_a733_gpu_fwboot_result)) ||
	    mem->dmat == NULL || !mem->interface_prepared ||
	    fwprobe_overlap(mem, sizeof(*mem), out, sizeof(*out)) ||
	    fwprobe_overlap(boot, sizeof(*boot), out, sizeof(*out)) ||
	    fwprobe_overlap(mem, sizeof(*mem), boot, sizeof(*boot)))
		return EINVAL;
	if (!boot->attempted || !boot->device_owned || boot->error != 0 ||
	    !boot->start.touched || !boot->start.released ||
	    !boot->snapshot_valid || boot->firmware_started != 1)
		return EINVAL;
	if (mem->image.info.bvnc != A733_EXPECTED_BVNC)
		return EOPNOTSUPP;
	dma[0] = &mem->code;
	dma[1] = &mem->table;
	dma[2] = &mem->data;
	dma[3] = &mem->work;
	dma[4] = &mem->config;
	for (i = 0; i < __arraycount(dma); i++) {
		if (!fwprobe_dma_valid(dma[i]) ||
		    fwprobe_overlap(dma[i]->cpu, dma[i]->size, mem, sizeof(*mem)) ||
		    fwprobe_overlap(dma[i]->cpu, dma[i]->size, boot, sizeof(*boot)) ||
		    fwprobe_overlap(dma[i]->cpu, dma[i]->size, out, sizeof(*out)))
			return EINVAL;
		if (i >= 3 && (!dma[i]->coherent ||
		    (dma[i]->map->_dm_flags & (_BUS_DMAMAP_COHERENT |
		    _BUS_DMAMAP_IS_BOUNCING)) != _BUS_DMAMAP_COHERENT))
			return EOPNOTSUPP;
		for (j = 0; j < i; j++) {
			if (dma[i]->map == dma[j]->map ||
			    fwprobe_overlap(dma[i]->cpu, dma[i]->size,
			    dma[j]->cpu, dma[j]->size) ||
			    (dma[i]->address < dma[j]->address + dma[j]->size &&
			    dma[j]->address < dma[i]->address + dma[i]->size))
				return EINVAL;
		}
	}
	if (!fwprobe_layout_valid(mem))
		return EINVAL;
	return 0;
}

static uint8_t *
fwprobe_object(const struct sun60i_a733_gpu_fwmem *mem, unsigned int id)
{
	const struct sun60i_a733_gpu_fwinit_region *r =
	    &mem->interface_layout.object[id];
	uint8_t *base = r->buffer == A733_GPU_FWINIT_WORK ?
	    mem->work.cpu : mem->config.cpu;

	return base + r->offset;
}

static uint32_t
fwprobe_read32(const void *p)
{
	return *(const volatile uint32_t *)p;
}

static uint64_t
fwprobe_read64(const void *p)
{
	return *(const volatile uint64_t *)p;
}

static void
fwprobe_observe(struct sun60i_a733_gpu_fwmem *mem,
    struct sun60i_a733_gpu_fwprobe_snapshot *s)
{
	const uint8_t *kc = fwprobe_object(mem, A733_GPU_FWINIT_KCCB_CTL);
	const uint8_t *fc = fwprobe_object(mem, A733_GPU_FWINIT_FWCCB_CTL);
	const uint8_t *sd = fwprobe_object(mem, A733_GPU_FWINIT_SYSDATA);
	const uint8_t *od = fwprobe_object(mem, A733_GPU_FWINIT_OSDATA);
	const uint8_t *tc = fwprobe_object(mem, A733_GPU_FWINIT_TRACE_CTL);
	const uint8_t *cn = fwprobe_object(mem, A733_GPU_FWINIT_CONNECTION);
	const uint8_t *si = fwprobe_object(mem, A733_GPU_FWINIT_SYSINIT);
	const uint8_t *cc = fwprobe_object(mem, A733_GPU_FWINIT_OSINIT) +
	    offsetof(struct rogue_fwif_osinit, rogue_comp_checks);

	bus_dmamap_sync(mem->dmat, mem->work.map, 0, mem->work.size,
	    BUS_DMASYNC_POSTREAD);
	bus_dmamap_sync(mem->dmat, mem->config.map, 0, mem->config.size,
	    BUS_DMASYNC_POSTREAD);
#define READ32(base, type, field) \
	fwprobe_read32((base) + offsetof(struct type, field))
#define READ64(base, type, field) \
	fwprobe_read64((base) + offsetof(struct type, field))
	s->read_offset = READ32(kc, rogue_fwif_ccb_ctl, read_offset);
	s->write_offset = READ32(kc, rogue_fwif_ccb_ctl, write_offset);
	s->wrap_mask = READ32(kc, rogue_fwif_ccb_ctl, wrap_mask);
	s->cmd_size = READ32(kc, rogue_fwif_ccb_ctl, cmd_size);
	s->rtn = fwprobe_read32(fwprobe_object(mem, A733_GPU_FWINIT_KCCB_RTN));
	s->fw_read_offset = READ32(fc, rogue_fwif_ccb_ctl, read_offset);
	s->fw_write_offset = READ32(fc, rogue_fwif_ccb_ctl, write_offset);
	s->fw_wrap_mask = READ32(fc, rogue_fwif_ccb_ctl, wrap_mask);
	s->fw_cmd_size = READ32(fc, rogue_fwif_ccb_ctl, cmd_size);
	s->fw_command = 0;
	if (s->fw_read_offset < A733_GPU_FWINIT_FWCCB_COUNT &&
	    s->fw_read_offset != s->fw_write_offset)
		s->fw_command = fwprobe_read32(fwprobe_object(mem,
		    A733_GPU_FWINIT_FWCCB_CMDS) + s->fw_read_offset *
		    sizeof(struct rogue_fwif_fwccb_cmd));
	s->cmds_executed = READ32(od, rogue_fwif_osdata, kccb_cmds_executed);
	s->pow_state = READ32(sd, rogue_fwif_sysdata, pow_state);
	s->hwr_state = READ32(sd, rogue_fwif_sysdata, hwr_state_flags);
	s->fw_faults = READ32(sd, rogue_fwif_sysdata, fw_faults);
	s->log_type = READ32(tc, rogue_fwif_tracebuf, log_type);
	s->fw_state = READ32(cn, rogue_fwif_connection_ctl, connection_fw_state);
	s->os_state = READ32(cn, rogue_fwif_connection_ctl, connection_os_state);
	s->alive_fw_token = READ32(cn, rogue_fwif_connection_ctl, alive_fw_token);
	s->alive_os_token = READ32(cn, rogue_fwif_connection_ctl, alive_os_token);
	s->firmware_started = *(const volatile uint8_t *)(si +
	    offsetof(struct rogue_fwif_sysinit, firmware_started));
	s->compchecks_updated = *(const volatile uint8_t *)(cc +
	    offsetof(struct rogue_fwif_compchecks, updated));
	/* Order publication-flag load before the dependent DMA payload loads. */
	bus_dmamap_sync(mem->dmat, mem->config.map, 0, mem->config.size,
	    BUS_DMASYNC_POSTREAD);
	s->os_count_support = *(const volatile uint8_t *)(cc +
	    offsetof(struct rogue_fwif_compchecks, init_options.os_count_support));
	s->hw_layout_version =
	    READ32(cc, rogue_fwif_compchecks, hw_bvnc.layout_version);
	s->fw_layout_version =
	    READ32(cc, rogue_fwif_compchecks, fw_bvnc.layout_version);
	s->hw_bvnc = READ64(cc, rogue_fwif_compchecks, hw_bvnc.bvnc);
	s->fw_bvnc = READ64(cc, rogue_fwif_compchecks, fw_bvnc.bvnc);
	s->fw_processor_version =
	    READ32(cc, rogue_fwif_compchecks, fw_processor_version);
	s->ddk_version = READ32(cc, rogue_fwif_compchecks, ddk_version);
	s->ddk_build = READ32(cc, rogue_fwif_compchecks, ddk_build);
	s->build_options = READ32(cc, rogue_fwif_compchecks, build_options);
#undef READ32
#undef READ64
	bus_dmamap_sync(mem->dmat, mem->work.map, 0, mem->work.size,
	    BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(mem->dmat, mem->config.map, 0, mem->config.size,
	    BUS_DMASYNC_PREREAD);
}

static int
fwprobe_runtime_valid(const struct sun60i_a733_gpu_fwprobe_snapshot *s)
{
	if (s->firmware_started != 1 || s->compchecks_updated > 1)
		return EPROTO;
	if (s->compchecks_updated == 0)
		return EAGAIN;
	if (s->hw_layout_version != ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION ||
	    s->fw_layout_version != ROGUE_FWIF_COMPCHECKS_LAYOUT_VERSION)
		return EOPNOTSUPP;
	if (s->hw_bvnc != A733_EXPECTED_BVNC || s->fw_bvnc != A733_EXPECTED_BVNC)
		return ENODEV;
	if (s->wrap_mask != A733_GPU_FWINIT_KCCB_COUNT - 1 ||
	    s->cmd_size != sizeof(struct rogue_fwif_kccb_cmd) ||
	    s->fw_wrap_mask != A733_GPU_FWINIT_FWCCB_COUNT - 1 ||
	    s->fw_cmd_size != sizeof(struct rogue_fwif_fwccb_cmd) ||
	    s->fw_read_offset >= A733_GPU_FWINIT_FWCCB_COUNT ||
	    s->fw_write_offset >= A733_GPU_FWINIT_FWCCB_COUNT ||
	    s->log_type != 0)
		return EPROTO;
	if (s->fw_read_offset != s->fw_write_offset || s->fw_faults != 0 ||
	    (s->hwr_state & A733_HWR_BAD) != 0)
		return EIO;
	if (s->pow_state != ROGUE_FWIF_POW_ON &&
	    s->pow_state != ROGUE_FWIF_POW_IDLE)
		return EBUSY;
	return 0;
}

/* Only these exact CPU-owned ranges are written. Other FW fields stay live. */
static void
fwprobe_publish(struct sun60i_a733_gpu_fwmem *mem,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	const struct sun60i_a733_gpu_fwinit_layout *l = &mem->interface_layout;
	struct rogue_fwif_kccb_cmd cmd = { 0 };
	const size_t command = l->object[A733_GPU_FWINIT_KCCB_CMDS].offset;
	const size_t rtn = l->object[A733_GPU_FWINIT_KCCB_RTN].offset;
	const size_t write = l->object[A733_GPU_FWINIT_KCCB_CTL].offset +
	    offsetof(struct rogue_fwif_ccb_ctl, write_offset);
	uint8_t *bytes = mem->work.cpu;

	cmd.cmd_type = ROGUE_FWIF_KCCB_CMD_LOGTYPE_UPDATE;
	/* Initial empty queue gives the host exclusive ownership of slot 0. */
	bus_dmamap_sync(mem->dmat, mem->work.map, command, sizeof(cmd),
	    BUS_DMASYNC_POSTWRITE);
	memcpy(bytes + command, &cmd, sizeof(cmd));
	bus_dmamap_sync(mem->dmat, mem->work.map, rtn, sizeof(uint32_t),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	*(volatile uint32_t *)(bytes + rtn) = ROGUE_FWIF_KCCB_RTN_SLOT_NO_RESPONSE;
	bus_dmamap_sync(mem->dmat, mem->work.map, command, sizeof(cmd),
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(mem->dmat, mem->work.map, rtn, sizeof(uint32_t),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(mem->dmat, mem->work.map, write, sizeof(uint32_t),
	    BUS_DMASYNC_POSTWRITE);
	/* Firmware may consume this slot before the subsequent MMIO kick. */
	out->published = true;
	*(volatile uint32_t *)(bytes + write) = 1;
	bus_dmamap_sync(mem->dmat, mem->work.map, write, sizeof(uint32_t),
	    BUS_DMASYNC_PREWRITE);
}

int
sun60i_a733_gpu_fwprobe(bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	struct timespec start, now, elapsed;
	int error;

	if (!fwprobe_span(out, sizeof(*out),
	    _Alignof(struct sun60i_a733_gpu_fwprobe_result)))
		return EINVAL;
	if (out->attempted || out->published || out->kick_attempted || out->kicked)
		return EBUSY;
	if (bst == NULL)
		return EINVAL;
	error = fwprobe_validate(mem, boot, out);
	if (error != 0)
		return error;
	memset(out, 0, sizeof(*out));
	out->attempted = true;
	getnanouptime(&start);
	error = ETIMEDOUT;
	while (out->polls < A733_GPU_FWPROBE_MAX_POLLS) {
		uint64_t remaining_ns;
		unsigned int wait_us;
		int state_error;

		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		if (elapsed.tv_sec >= 1)
			break;
		fwprobe_observe(mem, &out->after);
		out->polls++;
		if (!out->published)
			out->before = out->after;
		state_error = fwprobe_runtime_valid(&out->after);
		if (state_error != 0 && state_error != EAGAIN) {
			error = state_error;
			break;
		}
		if (out->published) {
			if (state_error == EAGAIN || out->after.write_offset != 1 ||
			    out->after.read_offset > 1 || (out->after.rtn &
			    ~ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) != 0) {
				error = EPROTO;
				break;
			}
			if (out->after.rtn == ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED &&
			    out->after.read_offset == 1) {
				error = 0;
				break;
			}
		} else if (state_error == 0) {
			if (out->before.read_offset != 0 ||
			    out->before.write_offset != 0) {
				error = EBUSY;
				break;
			}
			/* Do not publish when no completion budget remains. */
			getnanouptime(&now);
			timespecsub(&now, &start, &elapsed);
			if (elapsed.tv_sec >= 1 ||
			    out->polls == A733_GPU_FWPROBE_MAX_POLLS)
				break;
			fwprobe_publish(mem, out);
			out->kick_attempted = true;
			bus_space_barrier(bst, bsh, A733_MTS_SCHEDULE,
			    sizeof(uint32_t),
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			if (bus_space_poke_4(bst, bsh, A733_MTS_SCHEDULE, 0) != 0) {
				error = EFAULT;
				break;
			}
			bus_space_barrier(bst, bsh, A733_MTS_SCHEDULE,
			    sizeof(uint32_t),
			    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
			out->kicked = true;
			continue;
		}
		if (out->polls == A733_GPU_FWPROBE_MAX_POLLS)
			break;
		getnanouptime(&now);
		timespecsub(&now, &start, &elapsed);
		if (elapsed.tv_sec >= 1)
			break;
		remaining_ns = UINT64_C(1000000000) - elapsed.tv_nsec;
		wait_us = (unsigned int)(remaining_ns / 1000);
		if (wait_us > A733_GPU_FWPROBE_POLL_US)
			wait_us = A733_GPU_FWPROBE_POLL_US;
		if (wait_us != 0)
			delay(wait_us);
	}
	out->error = error;
	return error;
}
