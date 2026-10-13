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
#define A733_MMUCACHE_FLAGS	(ROGUE_FWIF_MMUCACHEDATA_FLAGS_PT | \
    ROGUE_FWIF_MMUCACHEDATA_FLAGS_PD | ROGUE_FWIF_MMUCACHEDATA_FLAGS_PC | \
    ROGUE_FWIF_MMUCACHEDATA_FLAGS_TLB | ROGUE_FWIF_MMUCACHEDATA_FLAGS_INTERRUPT)
__CTASSERT(A733_MMUCACHE_FLAGS == 0x0400001f);
__CTASSERT(A733_GPU_FWPROBE_LOGTYPE == ROGUE_FWIF_KCCB_CMD_LOGTYPE_UPDATE);
__CTASSERT(A733_GPU_FWPROBE_MMUCACHE == ROGUE_FWIF_KCCB_CMD_MMUCACHE);
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
    const void *out, size_t out_size)
{
	const struct sun60i_a733_gpu_dma *dma[5];
	size_t i, j;

	if (!fwprobe_span(mem, sizeof(*mem), _Alignof(struct sun60i_a733_gpu_fwmem)) ||
	    !fwprobe_span(boot, sizeof(*boot),
	    _Alignof(struct sun60i_a733_gpu_fwboot_result)) ||
	    mem->dmat == NULL || !mem->interface_prepared ||
	    fwprobe_overlap(mem, sizeof(*mem), out, out_size) ||
	    fwprobe_overlap(boot, sizeof(*boot), out, out_size) ||
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
		    fwprobe_overlap(dma[i]->cpu, dma[i]->size, out, out_size))
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
fwprobe_observe(struct sun60i_a733_gpu_fwmem *mem, unsigned int slot,
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
	s->rtn = fwprobe_read32(fwprobe_object(mem, A733_GPU_FWINIT_KCCB_RTN) +
	    slot * sizeof(uint32_t));
	s->mmucache_sync = fwprobe_read32(fwprobe_object(mem,
	    A733_GPU_FWINIT_MMUCACHE_SYNC));
	s->fw_read_offset = READ32(fc, rogue_fwif_ccb_ctl, read_offset);
	s->fw_write_offset = READ32(fc, rogue_fwif_ccb_ctl, write_offset);
	s->fw_wrap_mask = READ32(fc, rogue_fwif_ccb_ctl, wrap_mask);
	s->fw_cmd_size = READ32(fc, rogue_fwif_ccb_ctl, cmd_size);
	s->fw_command = 0;
	s->fw_flags = 0;
	s->fw_freelist_id = 0;
	if (s->fw_read_offset < A733_GPU_FWINIT_FWCCB_COUNT &&
	    s->fw_read_offset != s->fw_write_offset) {
		const uint8_t *event = fwprobe_object(mem,
		    A733_GPU_FWINIT_FWCCB_CMDS) + s->fw_read_offset *
		    sizeof(struct rogue_fwif_fwccb_cmd);

		s->fw_flags = fwprobe_read32(event +
		    offsetof(struct rogue_fwif_fwccb_cmd, fwccb_flags));
		s->fw_freelist_id = fwprobe_read32(event +
		    offsetof(struct rogue_fwif_fwccb_cmd,
		    cmd_data.cmd_free_list_gs.freelist_id));
		s->fw_command = fwprobe_read32(fwprobe_object(mem,
		    A733_GPU_FWINIT_FWCCB_CMDS) + s->fw_read_offset *
		    sizeof(struct rogue_fwif_fwccb_cmd));
	}
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
	s->firmware_started_timestamp =
	    READ32(si, rogue_fwif_sysinit, firmware_started_timestamp);
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

static bool
fwqueue_same_firmware(const struct sun60i_a733_gpu_fwprobe_snapshot *a,
    const struct sun60i_a733_gpu_fwprobe_snapshot *b)
{
	return a->firmware_started == b->firmware_started &&
	    a->firmware_started_timestamp == b->firmware_started_timestamp &&
	    a->compchecks_updated == b->compchecks_updated &&
	    a->os_count_support == b->os_count_support &&
	    a->hw_layout_version == b->hw_layout_version &&
	    a->fw_layout_version == b->fw_layout_version &&
	    a->hw_bvnc == b->hw_bvnc && a->fw_bvnc == b->fw_bvnc &&
	    a->fw_processor_version == b->fw_processor_version &&
	    a->ddk_version == b->ddk_version && a->ddk_build == b->ddk_build &&
	    a->build_options == b->build_options;
}

static int
fwprobe_runtime_valid(const struct sun60i_a733_gpu_fwprobe_snapshot *s,
    uint32_t expected_log_type, bool events)
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
	    s->log_type != expected_log_type)
		return EPROTO;
	if ((s->fw_read_offset != s->fw_write_offset &&
	    (!events || s->fw_command != ROGUE_FWIF_FWCCB_CMD_FREELIST_GROW ||
	    s->fw_flags != 0 || s->fw_freelist_id == 0)) || s->fw_faults != 0 ||
	    (s->hwr_state & A733_HWR_BAD) != 0)
		return EIO;
	if (s->pow_state != ROGUE_FWIF_POW_ON &&
	    s->pow_state != ROGUE_FWIF_POW_IDLE)
		return EBUSY;
	return 0;
}

/*
 * Linux pvr_cccb_send_kccb_kick() submits KICK with no return-slot waiter.
 * Firmware consumes the KCCB entry, then finishes work through CCCB/UFOs.
 * Keep synchronous LOGTYPE/MMUCACHE return checks and reject status errors.
 */
static bool
fwprobe_return_valid(uint32_t command, uint32_t rtn)
{
	if (command != A733_GPU_FWPROBE_LOGTYPE &&
	    command != A733_GPU_FWPROBE_MMUCACHE &&
	    command != ROGUE_FWIF_KCCB_CMD_KICK)
		return false;
	return (rtn & ~ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) == 0 &&
	    (command == ROGUE_FWIF_KCCB_CMD_KICK ||
	    rtn == ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED);
}

int
sun60i_a733_gpu_fwprobe_observe(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot, unsigned int slot,
    uint32_t command, uint32_t expected_log_type,
    struct sun60i_a733_gpu_fwprobe_observation *out)
{
	struct sun60i_a733_gpu_fwprobe_observation observation;
	int error;

	if (!fwprobe_span(out, sizeof(*out), _Alignof(struct sun60i_a733_gpu_fwprobe_observation)) ||
	    slot >= A733_GPU_FWINIT_KCCB_COUNT ||
	    !fwprobe_return_valid(command, ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) ||
	    (expected_log_type & ~ROGUE_FWIF_LOG_TYPE_MASK) != 0 ||
	    (expected_log_type != 0 &&
	    ((expected_log_type & ROGUE_FWIF_LOG_TYPE_TRACE) == 0 ||
	    (expected_log_type & ROGUE_FWIF_LOG_TYPE_GROUP_MASK) == 0)))
		return EINVAL;
	error = fwprobe_validate(mem, boot, out, sizeof(*out));
	if (error != 0)
		return error;
	memset(&observation, 0, sizeof(observation));
	fwprobe_observe(mem, slot, &observation.state);
	observation.valid = true;
	error = fwprobe_runtime_valid(&observation.state, expected_log_type, false);
	if (error == 0 &&
	    (observation.state.read_offset != slot + 1 ||
	    observation.state.write_offset != slot + 1 ||
	    !fwprobe_return_valid(command, observation.state.rtn)))
		error = EPROTO;
	observation.error = error;
	*out = observation;
	return error;
}

/* The first successful result is immutable and belongs to this boot's owner. */
static int
fwprobe_previous_valid(const struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *previous,
    const struct sun60i_a733_gpu_fwprobe_result *out)
{
	const struct sun60i_a733_gpu_dma *dma[] = {
		&mem->code, &mem->table, &mem->data, &mem->work, &mem->config
	};
	size_t i;

	if (!fwprobe_span(previous, sizeof(*previous),
	    _Alignof(struct sun60i_a733_gpu_fwprobe_result)) ||
	    fwprobe_overlap(previous, sizeof(*previous), out, sizeof(*out)) ||
	    fwprobe_overlap(previous, sizeof(*previous), mem, sizeof(*mem)) ||
	    fwprobe_overlap(previous, sizeof(*previous), boot, sizeof(*boot)))
		return EINVAL;
	for (i = 0; i < __arraycount(dma); i++) {
		if (fwprobe_overlap(previous, sizeof(*previous),
		    dma[i]->cpu, dma[i]->size))
			return EINVAL;
	}
	if (!previous->attempted || !previous->published ||
	    !previous->kick_attempted || !previous->kicked || previous->error != 0 ||
	    previous->command != A733_GPU_FWPROBE_LOGTYPE || previous->slot != 0 ||
	    previous->cache_flags != 0 || previous->sync_fw_addr != 0 ||
	    previous->sync_update_value != 0 || previous->polls < 2 ||
	    previous->polls > A733_GPU_FWPROBE_MAX_POLLS ||
	    previous->before.read_offset != 0 || previous->before.write_offset != 0 ||
	    previous->after.read_offset != 1 || previous->after.write_offset != 1 ||
	    previous->after.rtn != ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED ||
	    fwprobe_runtime_valid(&previous->before, 0, false) != 0 ||
	    fwprobe_runtime_valid(&previous->after, 0, false) != 0)
		return EINVAL;
	return 0;
}

/* Only these exact CPU-owned ranges are written. Other FW fields stay live. */
static void
fwprobe_publish(struct sun60i_a733_gpu_fwmem *mem,
    const struct rogue_fwif_kccb_cmd *cmd, unsigned int next,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	const struct sun60i_a733_gpu_fwinit_layout *l = &mem->interface_layout;
	const size_t command = l->object[A733_GPU_FWINIT_KCCB_CMDS].offset +
	    out->slot * sizeof(*cmd);
	const size_t rtn = l->object[A733_GPU_FWINIT_KCCB_RTN].offset +
	    out->slot * sizeof(uint32_t);
	const size_t write = l->object[A733_GPU_FWINIT_KCCB_CTL].offset +
	    offsetof(struct rogue_fwif_ccb_ctl, write_offset);
	uint8_t *bytes = mem->work.cpu;
	/* The validated empty queue gives the host ownership of this slot. */
	bus_dmamap_sync(mem->dmat, mem->work.map, command, sizeof(*cmd),
	    BUS_DMASYNC_POSTWRITE);
	memcpy(bytes + command, cmd, sizeof(*cmd));
	bus_dmamap_sync(mem->dmat, mem->work.map, rtn, sizeof(uint32_t),
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	*(volatile uint32_t *)(bytes + rtn) = ROGUE_FWIF_KCCB_RTN_SLOT_NO_RESPONSE;
	bus_dmamap_sync(mem->dmat, mem->work.map, command, sizeof(*cmd),
	    BUS_DMASYNC_PREWRITE);
	bus_dmamap_sync(mem->dmat, mem->work.map, rtn, sizeof(uint32_t),
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	bus_dmamap_sync(mem->dmat, mem->work.map, write, sizeof(uint32_t),
	    BUS_DMASYNC_POSTWRITE);
	/* Firmware may consume this slot before the subsequent MMIO kick. */
	out->published = true;
	*(volatile uint32_t *)(bytes + write) = next;
	bus_dmamap_sync(mem->dmat, mem->work.map, write, sizeof(uint32_t),
	    BUS_DMASYNC_PREWRITE);
}

/* A later command chains onto the fully successful predecessor result. */
static int
fwprobe_chain_valid(const struct sun60i_a733_gpu_fwprobe_result *previous,
    unsigned int slot)
{
	if (previous == NULL || !previous->attempted || !previous->published ||
	    !previous->kick_attempted || !previous->kicked ||
	    previous->error != 0 || previous->slot + 1 != slot ||
	    previous->after.read_offset != slot ||
	    previous->after.write_offset != slot ||
	    !fwprobe_return_valid(previous->command, previous->after.rtn))
		return EINVAL;
	return 0;
}

static int
fwprobe_run(bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *previous,
    const struct rogue_fwif_kccb_cmd *cmd, unsigned int slot,
    uint32_t expected_log_type, bool reusable, uint32_t fw_read,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	struct rogue_fwif_kccb_cmd built;
	unsigned int next = reusable ?
	    (slot + 1) & (A733_GPU_FWINIT_KCCB_COUNT - 1) : slot + 1;
	struct timespec start, now, elapsed;
	int error;

	if (!fwprobe_span(out, sizeof(*out),
	    _Alignof(struct sun60i_a733_gpu_fwprobe_result)))
		return EINVAL;
	if (out->attempted || out->published || out->kick_attempted || out->kicked)
		return EBUSY;
	if (bst == NULL)
		return EINVAL;
	error = fwprobe_validate(mem, boot, out, sizeof(*out));
	if (error != 0)
		return error;
	if (!reusable && slot == 1) {
		error = fwprobe_previous_valid(mem, boot, previous, out);
		if (error != 0)
			return error;
	} else if (!reusable && slot >= 2) {
		error = fwprobe_chain_valid(previous, slot);
		if (error != 0)
			return error;
		/* Only LOGTYPE_UPDATE can change the previous logging mode. */
		if (cmd != NULL && cmd->cmd_type != A733_GPU_FWPROBE_LOGTYPE &&
		    previous->after.log_type != expected_log_type)
			return EINVAL;
	}
	if (slot >= A733_GPU_FWINIT_KCCB_COUNT)
		return EINVAL;
	memset(out, 0, sizeof(*out));
	out->slot = slot;
	if (cmd == NULL) {
		memset(&built, 0, sizeof(built));
		built.cmd_type = slot == 0 ? A733_GPU_FWPROBE_LOGTYPE :
		    A733_GPU_FWPROBE_MMUCACHE;
		if (slot == 1) {
			built.cmd_data.mmu_cache_data.cache_flags =
			    A733_MMUCACHE_FLAGS;
			built.cmd_data.mmu_cache_data.mmu_cache_sync_fw_addr =
			    mem->interface_layout.object[
			    A733_GPU_FWINIT_MMUCACHE_SYNC].fw_addr;
			/* Pinned upstream uses zero. The return proves completion. */
			built.cmd_data.mmu_cache_data.
			    mmu_cache_sync_update_value = 0;
		}
		cmd = &built;
		if (slot == 1) {
			out->cache_flags = A733_MMUCACHE_FLAGS;
			out->sync_fw_addr = built.cmd_data.mmu_cache_data.
			    mmu_cache_sync_fw_addr;
			out->sync_update_value = 0;
		}
	}
	if (cmd->cmd_type != A733_GPU_FWPROBE_LOGTYPE &&
	    cmd->cmd_type != A733_GPU_FWPROBE_MMUCACHE &&
	    cmd->cmd_type != ROGUE_FWIF_KCCB_CMD_KICK &&
	    (!reusable || (cmd->cmd_type != ROGUE_FWIF_KCCB_CMD_CLEANUP &&
	    cmd->cmd_type != ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE)))
		return EINVAL;
	out->command = cmd->cmd_type;
	if (reusable && cmd->cmd_type == A733_GPU_FWPROBE_MMUCACHE) {
		out->cache_flags = cmd->cmd_data.mmu_cache_data.cache_flags;
		out->sync_fw_addr = cmd->cmd_data.mmu_cache_data.
		    mmu_cache_sync_fw_addr;
		out->sync_update_value = cmd->cmd_data.mmu_cache_data.
		    mmu_cache_sync_update_value;
	}
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
		fwprobe_observe(mem, slot, &out->after);
		out->polls++;
		if (!out->published)
			out->before = out->after;
		state_error = fwprobe_runtime_valid(&out->after,
		    expected_log_type, reusable);
		if (reusable && out->after.fw_read_offset != fw_read)
			state_error = EPROTO;
		if (state_error != 0 && state_error != EAGAIN) {
			error = state_error;
			break;
		}
		if (reusable && previous != NULL &&
		    !fwqueue_same_firmware(&previous->after, &out->after)) {
			error = EPROTO;
			break;
		}
		if (out->published) {
			uint32_t allowed = ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED;

			if (reusable && out->command == ROGUE_FWIF_KCCB_CMD_CLEANUP)
				allowed |= ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY;
			if (state_error == EAGAIN || out->after.write_offset != next ||
			    (out->after.read_offset != slot && out->after.read_offset != next) ||
			    (out->after.rtn & ~allowed) != 0) {
				error = EPROTO;
				break;
			}
			if ((fwprobe_return_valid(out->command, out->after.rtn) ||
			    (reusable && out->command ==
			    ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE &&
			    (out->after.rtn & ~ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) == 0) ||
			    (reusable && out->command == ROGUE_FWIF_KCCB_CMD_CLEANUP &&
			    (out->after.rtn & ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) != 0)) &&
			    out->after.read_offset == next) {
				error = (out->after.rtn &
				    ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY) != 0 ? EBUSY : 0;
				break;
			}
		} else if (state_error == 0) {
			if (out->before.read_offset != slot ||
			    out->before.write_offset != slot) {
				error = EBUSY;
				break;
			}
			/* Do not publish when no completion budget remains. */
			getnanouptime(&now);
			timespecsub(&now, &start, &elapsed);
			if (elapsed.tv_sec >= 1 ||
			    out->polls == A733_GPU_FWPROBE_MAX_POLLS)
				break;
			fwprobe_publish(mem, cmd, next, out);
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

int
sun60i_a733_gpu_fwprobe(bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	return fwprobe_run(bst, bsh, mem, boot, NULL, NULL, 0, 0, false, 0, out);
}

int
sun60i_a733_gpu_fwprobe_mmucache(bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *logtype,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	return fwprobe_run(bst, bsh, mem, boot, logtype, NULL, 1, 0, false, 0, out);
}

int
sun60i_a733_gpu_fwprobe_cmd(bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *previous,
    const struct rogue_fwif_kccb_cmd *cmd, unsigned int slot,
    uint32_t expected_log_type,
    struct sun60i_a733_gpu_fwprobe_result *out)
{
	if (cmd == NULL || slot < 2 ||
	    (expected_log_type & ~ROGUE_FWIF_LOG_TYPE_MASK) != 0 ||
	    (expected_log_type != 0 &&
	    ((expected_log_type & ROGUE_FWIF_LOG_TYPE_TRACE) == 0 ||
	    (expected_log_type & ROGUE_FWIF_LOG_TYPE_GROUP_MASK) == 0)))
		return EINVAL;
	return fwprobe_run(bst, bsh, mem, boot, previous, cmd, slot,
	    expected_log_type, false, 0, out);
}

static bool
fwqueue_log_valid(uint32_t value)
{
	return (value & ~ROGUE_FWIF_LOG_TYPE_MASK) == 0 &&
	    (value == 0 || ((value & ROGUE_FWIF_LOG_TYPE_TRACE) != 0 &&
	    (value & ROGUE_FWIF_LOG_TYPE_GROUP_MASK) != 0));
}

static bool
fwqueue_return_valid(const struct sun60i_a733_gpu_fwprobe_result *r)
{
	if (r->command == ROGUE_FWIF_KCCB_CMD_CLEANUP)
		return (r->after.rtn == ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED &&
		    r->error == 0) ||
		    (r->after.rtn == (ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED |
		    ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY) && r->error == EBUSY);
	if (r->command == ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE)
		return r->error == 0 &&
		    (r->after.rtn & ~ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) == 0;
	return r->error == 0 && fwprobe_return_valid(r->command, r->after.rtn);
}

static bool
fwqueue_last_valid(const struct sun60i_a733_gpu_fwprobe_result *r)
{
	unsigned int next;

	if (r->slot >= A733_GPU_FWINIT_KCCB_COUNT)
		return false;
	next = (r->slot + 1) & (A733_GPU_FWINIT_KCCB_COUNT - 1);
	return r->attempted && r->published && r->kick_attempted && r->kicked &&
	    r->polls >= 2 && r->polls <= A733_GPU_FWPROBE_MAX_POLLS &&
	    r->before.read_offset == r->slot && r->before.write_offset == r->slot &&
	    r->after.read_offset == next && r->after.write_offset == next &&
	    fwqueue_log_valid(r->after.log_type) &&
	    fwprobe_runtime_valid(&r->before, r->after.log_type, true) == 0 &&
	    fwprobe_runtime_valid(&r->after, r->after.log_type, true) == 0 &&
	    fwqueue_same_firmware(&r->before, &r->after) && fwqueue_return_valid(r);
}

/* Validate all spans before any snapshot, memset or DMA operation. */
static int
fwqueue_validate(const struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwqueue *q, const void *out, size_t size,
    size_t alignment)
{
	int error;

	if (!fwprobe_span(q, sizeof(*q), _Alignof(struct sun60i_a733_gpu_fwqueue)) ||
	    !fwprobe_span(out, size, alignment) ||
	    fwprobe_overlap(q, sizeof(*q), out, size))
		return EINVAL;
	error = fwprobe_validate(mem, boot, q, sizeof(*q));
	if (error != 0)
		return error;
	return fwprobe_validate(mem, boot, out, size);
}

static int
fwqueue_owner_valid(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwqueue *q)
{
	if (!q->attached)
		return ENXIO;
	if (q->failed)
		return EIO;
	if (q->owner != mem || q->boot != boot ||
	    memcmp(&q->mem_identity, mem, sizeof(*mem)) != 0 ||
	    memcmp(&q->boot_identity, boot, sizeof(*boot)) != 0 ||
	    !fwqueue_last_valid(&q->last) ||
	    q->expected_log_type != q->last.after.log_type) {
		q->failed = true;
		return EINVAL;
	}
	return 0;
}

static int
fwqueue_observe(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwqueue *q,
    struct sun60i_a733_gpu_fwprobe_observation *out)
{
	struct sun60i_a733_gpu_fwprobe_result check;
	unsigned int next = (q->last.slot + 1) & (A733_GPU_FWINIT_KCCB_COUNT - 1);
	int error;

	memset(out, 0, sizeof(*out));
	fwprobe_observe(mem, q->last.slot, &out->state);
	out->valid = true;
	error = fwprobe_runtime_valid(&out->state, q->expected_log_type, true);
	check = q->last;
	check.after = out->state;
	if (error == 0 && (out->state.fw_read_offset != q->fw_read_offset ||
	    out->state.read_offset != next ||
	    out->state.write_offset != next || !fwqueue_return_valid(&check) ||
	    !fwqueue_same_firmware(&q->last.after, &out->state) ||
	    out->state.firmware_started_timestamp != boot->firmware_started_timestamp))
		error = EPROTO;
	out->error = error;
	if (error != 0)
		q->failed = true;
	return error;
}

int
sun60i_a733_gpu_fwqueue_attach(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    const struct sun60i_a733_gpu_fwprobe_result *previous,
    struct sun60i_a733_gpu_fwqueue *q)
{
	struct sun60i_a733_gpu_fwprobe_observation observation;
	int error;

	error = fwqueue_validate(mem, boot, q, previous, sizeof(*previous),
	    _Alignof(struct sun60i_a733_gpu_fwprobe_result));
	if (error != 0)
		return error;
	if (q->attached || q->failed)
		return EBUSY;
	if (previous->error != 0 || !fwqueue_last_valid(previous) ||
	    fwprobe_runtime_valid(&previous->before, previous->after.log_type, false) != 0 ||
	    fwprobe_runtime_valid(&previous->after, previous->after.log_type, false) != 0 ||
	    previous->after.firmware_started_timestamp != boot->firmware_started_timestamp)
		return EINVAL;
	q->attached = true;
	q->owner = mem;
	q->boot = boot;
	memcpy(&q->mem_identity, mem, sizeof(*mem));
	memcpy(&q->boot_identity, boot, sizeof(*boot));
	memcpy(&q->last, previous, sizeof(*previous));
	q->expected_log_type = previous->after.log_type;
	q->fw_read_offset = previous->after.fw_read_offset;
	error = fwqueue_observe(mem, boot, q, &observation);
	if (error == 0 && observation.state.fw_read_offset !=
	    observation.state.fw_write_offset) {
		q->failed = true;
		error = EIO;
	}
	return error;
}

/* Copy only supported fields. Never publish caller padding or inactive data. */
static int
fwqueue_command(const struct sun60i_a733_gpu_fwmem *mem,
    const struct rogue_fwif_kccb_cmd *cmd, struct rogue_fwif_kccb_cmd *built)
{
	const struct rogue_fwif_kccb_cmd_kick_data *kick;
	struct rogue_fwif_kccb_cmd_kick_data *out;
	unsigned int i;

	if (cmd->kccb_flags != 0)
		return EOPNOTSUPP;
	memset(built, 0, sizeof(*built));
	built->cmd_type = cmd->cmd_type;
	switch (cmd->cmd_type) {
	case ROGUE_FWIF_KCCB_CMD_LOGTYPE_UPDATE:
		return 0;
	case ROGUE_FWIF_KCCB_CMD_MMUCACHE:
		if (cmd->cmd_data.mmu_cache_data.cache_flags == 0 ||
		    (cmd->cmd_data.mmu_cache_data.cache_flags &
		    ~(A733_MMUCACHE_FLAGS | ROGUE_FWIF_MMUCACHEDATA_FLAGS_CTX_ALL)) != 0 ||
		    cmd->cmd_data.mmu_cache_data.mmu_cache_sync_fw_addr !=
		    mem->interface_layout.object[A733_GPU_FWINIT_MMUCACHE_SYNC].fw_addr)
			return EINVAL;
		built->cmd_data.mmu_cache_data = cmd->cmd_data.mmu_cache_data;
		return 0;
	case ROGUE_FWIF_KCCB_CMD_KICK:
		kick = &cmd->cmd_data.cmd_kick_data;
		if (kick->context_fw_addr == 0 || kick->context_fw_addr % 4 != 0 ||
		    kick->num_cleanup_ctl > ROGUE_FWIF_KCCB_CMD_KICK_DATA_MAX_NUM_CLEANUP_CTLS)
			return EINVAL;
		out = &built->cmd_data.cmd_kick_data;
		out->context_fw_addr = kick->context_fw_addr;
		out->client_woff_update = kick->client_woff_update;
		out->client_wrap_mask_update = kick->client_wrap_mask_update;
		out->num_cleanup_ctl = kick->num_cleanup_ctl;
		for (i = 0; i < kick->num_cleanup_ctl; i++)
			out->cleanup_ctl_fw_addr[i] = kick->cleanup_ctl_fw_addr[i];
		out->work_est_cmd_header_offset = kick->work_est_cmd_header_offset;
		return 0;
	case ROGUE_FWIF_KCCB_CMD_FREELIST_GROW_UPDATE: {
		const struct rogue_fwif_freelist_gs_data *grow =
		    &cmd->cmd_data.free_list_gs_data;

		if (grow->freelist_fw_addr == 0 || grow->freelist_fw_addr % 4 != 0 ||
		    grow->delta_pages > grow->new_pages ||
		    grow->ready_pages > grow->new_pages ||
		    ((grow->delta_pages | grow->new_pages | grow->ready_pages) & 3) != 0)
			return EINVAL;
		built->cmd_data.free_list_gs_data = *grow;
		return 0;
	}
	case ROGUE_FWIF_KCCB_CMD_CLEANUP:
		switch (cmd->cmd_data.cleanup_data.cleanup_type) {
		case ROGUE_FWIF_CLEANUP_FWCOMMONCONTEXT:
		case ROGUE_FWIF_CLEANUP_HWRTDATA:
		case ROGUE_FWIF_CLEANUP_FREELIST:
			break;
		default:
			return EOPNOTSUPP;
		}
		if (cmd->cmd_data.cleanup_data.cleanup_data.context_fw_addr == 0 ||
		    cmd->cmd_data.cleanup_data.cleanup_data.context_fw_addr % 4 != 0)
			return EINVAL;
		built->cmd_data.cleanup_data = cmd->cmd_data.cleanup_data;
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

int
sun60i_a733_gpu_fwqueue_cmd(bus_space_tag_t bst, bus_space_handle_t bsh,
    struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwqueue *q, const struct rogue_fwif_kccb_cmd *cmd,
    uint32_t expected_log_type, struct sun60i_a733_gpu_fwprobe_result *out)
{
	struct rogue_fwif_kccb_cmd built;
	unsigned int slot;
	int error;

	error = fwqueue_validate(mem, boot, q, out, sizeof(*out), _Alignof(struct sun60i_a733_gpu_fwprobe_result));
	if (error != 0)
		return error;
	error = fwqueue_validate(mem, boot, q, cmd, sizeof(*cmd), _Alignof(struct rogue_fwif_kccb_cmd));
	if (error != 0)
		return error;
	if (bst == NULL || !fwqueue_log_valid(expected_log_type) ||
	    fwprobe_overlap(cmd, sizeof(*cmd), out, sizeof(*out)))
		return EINVAL;
	error = fwqueue_command(mem, cmd, &built);
	if (error != 0)
		return error;
	error = fwqueue_owner_valid(mem, boot, q);
	if (error != 0) {
		memset(out, 0, sizeof(*out));
		out->error = error;
		return error;
	}
	if (built.cmd_type != ROGUE_FWIF_KCCB_CMD_LOGTYPE_UPDATE &&
	    expected_log_type != q->expected_log_type)
		return EINVAL;
	slot = (q->last.slot + 1) & (A733_GPU_FWINIT_KCCB_COUNT - 1);
	memset(out, 0, sizeof(*out));
	error = fwprobe_run(bst, bsh, mem, boot, &q->last, &built, slot,
	    expected_log_type, true, q->fw_read_offset, out);
	if (fwqueue_last_valid(out)) {
		memcpy(&q->last, out, sizeof(*out));
		q->expected_log_type = expected_log_type;
	} else {
		q->failed = true;
	}
	return error;
}

int
sun60i_a733_gpu_fwqueue_observe(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwqueue *q,
    struct sun60i_a733_gpu_fwprobe_observation *out)
{
	int error;

	error = fwqueue_validate(mem, boot, q, out, sizeof(*out), _Alignof(struct sun60i_a733_gpu_fwprobe_observation));
	if (error != 0)
		return error;
	error = fwqueue_owner_valid(mem, boot, q);
	if (error != 0) {
		memset(out, 0, sizeof(*out));
		out->error = error;
		return error;
	}
	return fwqueue_observe(mem, boot, q, out);
}

int
sun60i_a733_gpu_fwqueue_event(struct sun60i_a733_gpu_fwmem *mem,
    const struct sun60i_a733_gpu_fwboot_result *boot,
    struct sun60i_a733_gpu_fwqueue *q, uint32_t *id)
{
	struct sun60i_a733_gpu_fwprobe_observation observation;
	size_t offset;
	uint32_t value, next;
	int error;

	error = fwqueue_validate(mem, boot, q, id, sizeof(*id), _Alignof(uint32_t));
	if (error != 0)
		return error;
	error = fwqueue_owner_valid(mem, boot, q);
	if (error != 0)
		return error;
	error = fwqueue_observe(mem, boot, q, &observation);
	if (error != 0)
		return error;
	if (observation.state.fw_read_offset == observation.state.fw_write_offset)
		return ENOENT;
	/* Runtime validation admits only a complete, known GROW request. */
	value = observation.state.fw_freelist_id;
	next = (q->fw_read_offset + 1) & (A733_GPU_FWINIT_FWCCB_COUNT - 1);
	offset = mem->interface_layout.object[A733_GPU_FWINIT_FWCCB_CTL].offset +
	    offsetof(struct rogue_fwif_ccb_ctl, read_offset);
	bus_dmamap_sync(mem->dmat, mem->work.map, offset, sizeof(uint32_t),
	    BUS_DMASYNC_POSTWRITE);
	*(volatile uint32_t *)((uint8_t *)mem->work.cpu + offset) = next;
	bus_dmamap_sync(mem->dmat, mem->work.map, offset, sizeof(uint32_t),
	    BUS_DMASYNC_PREWRITE);
	q->fw_read_offset = next;
	/* Confirm the read publication and health before dispatching the copy. */
	error = fwqueue_observe(mem, boot, q, &observation);
	if (error != 0)
		return error;
	*id = value;
	return 0;
}
