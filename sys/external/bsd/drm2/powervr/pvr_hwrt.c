/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * AI-assisted native adaptation of the MIT alternative in Linux
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40: pvr_hwrt.c,
 * pvr_rogue_cr_defs_client.h and pvr_cccb.c. Native ownership is an
 * EmberBSD change. Register generation selects the A733 MC1 profile.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <arm/sunxi/powervr/pvr_rogue_fwif.h>
#include "pvr_backend.h"
#include "pvr_hwrt.h"

/* Register fields from the pinned client definitions. */
#define ROGUE_CR_TE_AA_Y_EN 0x00000004U
#define ROGUE_CR_TE_AA_X_EN 0x00000002U
#define ROGUE_CR_TE_MTILE1_X1_SHIFT 18
#define ROGUE_CR_TE_MTILE2_Y1_SHIFT 18
#define ROGUE_CR_TE_SCREEN_YMAX_SHIFT 12
#define ROGUE_CR_PPP_SCREEN_PIXYMAX_SHIFT 16
#define ROGUE_CR_ISP_MTILE_SIZE_X_SHIFT 16

struct pvr_hwrt_rt {
	struct pvr_fw_object data, srtc, raa;
	bool exposed, cleaned;
};

struct pvr_hwrt {
	const struct pvr_backend *backend;
	struct pvr_fw_object common;
	struct pvr_hwrt_rt rt[ROGUE_FWIF_NUM_RTDATAS];
	bool ready, destroying, failed;
};

static bool
pvr_hwrt_address(uint64_t address, unsigned alignment, bool optional)
{

	return (optional || address != 0) && address < (UINT64_C(1) << 40) &&
	    (address & (alignment - 1)) == 0;
}

static int
pvr_hwrt_validate(const struct pvr_hwrt_config *c)
{
	unsigned i;

	if (c->bvnc != UINT64_C(0x00240038006800b7))
		return EOPNOTSUPP;
	if (c->width == 0 || c->width > 8192 || c->height == 0 ||
	    c->height > 8192 || c->layers == 0 || c->layers > 2048 ||
	    (c->samples != 1 && c->samples != 2 && c->samples != 4) ||
	    c->tpc_size == 0 || c->region_header_size == 0 ||
	    (c->layers > 1 && c->tpc_stride == 0) ||
	    c->min_local_pages == 0 || c->local_pages < c->min_local_pages)
		return EINVAL;
	if (!pvr_hwrt_address(c->tpc_dev_addr, 64, false) ||
	    !pvr_hwrt_address(c->vheap_table_dev_addr, 16, false) ||
	    !pvr_hwrt_address(c->rtc_dev_addr, 64, c->layers == 1))
		return EINVAL;
	for (i = 0; i < __arraycount(c->rt); i++) {
		if (c->freelist_fw_addr[i] == 0 ||
		    (c->freelist_fw_addr[i] & 7) != 0 ||
		    !pvr_hwrt_address(c->rt[i].pm_mlist_dev_addr, 16, false) ||
		    !pvr_hwrt_address(c->rt[i].macrotile_array_dev_addr, 16, true) ||
		    !pvr_hwrt_address(c->rt[i].region_header_dev_addr, 64, false))
			return EINVAL;
	}
	return 0;
}

static int
pvr_hwrt_alloc(struct pvr_hwrt *hwrt, struct pvr_fw_object *obj,
    size_t size, unsigned alignment)
{
	const struct pvr_backend *be = hwrt->backend;
	int error;

	error = be->alloc(be->cookie, size, obj);
	if (error != 0)
		return error;
	if (obj->owner == NULL || obj->cpu == NULL || obj->size < size ||
	    obj->address == 0 || obj->address > UINT32_MAX - size + 1 ||
	    (obj->address & (alignment - 1)) != 0 ||
	    ((uintptr_t)obj->cpu & (alignment - 1)) != 0) {
		hwrt->failed = true;
		be->fail(be->cookie);
		return EPROTO;
	}
	return 0;
}

static uint64_t
pvr_hwrt_multisample(uint32_t samples, bool flip)
{
	static const uint8_t x[3][8] = {
		{ 8 }, { 12, 4 }, { 6, 14, 2, 10 },
	};
	static const uint8_t y[3][8] = {
		{ 8 }, { 12, 4 }, { 2, 6, 10, 14 },
	};
	unsigned index = samples == 4 ? 2 : samples == 2 ? 1 : 0;
	uint64_t value = 0;
	unsigned i;

	for (i = 0; i < 8; i++) {
		value |= (uint64_t)x[index][i] << (i * 8);
		value |= (uint64_t)(flip ? (16 - y[index][i]) & 15 :
		    y[index][i]) << (i * 8 + 4);
	}
	return value;
}

static void
pvr_hwrt_common_init(struct pvr_hwrt *hwrt, const struct pvr_hwrt_config *c)
{
	struct rogue_fwif_hwrtdata_common *fw = hwrt->common.cpu;
	uint32_t tiles_x = (c->width + 15) / 16;
	uint32_t tiles_y = (c->height + 15) / 16;
	uint32_t mtile_x = ((tiles_x + 7) / 8) * 2;
	uint32_t mtile_y = ((tiles_y + 7) / 8) * 2;
	uint32_t isp_x = mtile_x, isp_y = mtile_y;

	fw->screen_pixel_max = (c->width - 1) |
	    ((c->height - 1) << ROGUE_CR_PPP_SCREEN_PIXYMAX_SHIFT);
	fw->multi_sample_ctl = pvr_hwrt_multisample(c->samples, false);
	fw->flipped_multi_sample_ctl = pvr_hwrt_multisample(c->samples, true);
	fw->tpc_stride = c->tpc_stride;
	fw->tpc_size = c->tpc_size;
	fw->te_screen = (roundup2(tiles_x, 2) - 1) |
	    ((roundup2(tiles_y, 2) - 1) << ROGUE_CR_TE_SCREEN_YMAX_SHIFT);
	fw->mtile_stride = mtile_x * mtile_y;
	if (c->samples >= 2) {
		fw->teaa |= ROGUE_CR_TE_AA_Y_EN;
		isp_y <<= 1;
	}
	if (c->samples >= 4) {
		fw->teaa |= ROGUE_CR_TE_AA_X_EN;
		isp_x <<= 1;
	}
	fw->te_mtile1 = mtile_x << ROGUE_CR_TE_MTILE1_X1_SHIFT;
	fw->te_mtile2 = mtile_y << ROGUE_CR_TE_MTILE2_Y1_SHIFT;
	fw->isp_merge_lower_x = c->isp_merge_lower_x;
	fw->isp_merge_lower_y = c->isp_merge_lower_y;
	fw->isp_merge_upper_x = c->isp_merge_upper_x;
	fw->isp_merge_upper_y = c->isp_merge_upper_y;
	fw->isp_merge_scale_x = c->isp_merge_scale_x;
	fw->isp_merge_scale_y = c->isp_merge_scale_y;
	fw->rgn_header_size = c->region_header_size;
	fw->isp_mtile_size = (isp_x << ROGUE_CR_ISP_MTILE_SIZE_X_SHIFT) | isp_y;
}

int
pvr_hwrt_create(const struct pvr_backend *be, const struct pvr_hwrt_config *c,
    struct pvr_hwrt **owner)
{
	struct pvr_hwrt *hwrt;
	struct pvr_hwrt_rt *rt;
	struct rogue_fwif_hwrtdata *fw;
	unsigned i;
	int error;

	if (be == NULL || c == NULL || owner == NULL || *owner != NULL)
		return EINVAL;
	error = pvr_hwrt_validate(c);
	if (error != 0)
		return error;
	error = be->check(be->cookie);
	if (error != 0)
		return error;
	hwrt = kmem_zalloc(sizeof(*hwrt), KM_SLEEP);
	hwrt->backend = be;
	*owner = hwrt;
	error = pvr_hwrt_alloc(hwrt, &hwrt->common,
	    sizeof(struct rogue_fwif_hwrtdata_common), 8);
	if (error != 0)
		goto fail;
	pvr_hwrt_common_init(hwrt, c);
	be->sync(be->cookie, &hwrt->common,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	for (i = 0; i < __arraycount(hwrt->rt); i++) {
		rt = &hwrt->rt[i];
		if (c->layers > 1) {
			error = pvr_hwrt_alloc(hwrt, &rt->srtc,
			    c->layers * sizeof(uint32_t), 8);
			if (error != 0)
				goto fail;
			error = pvr_hwrt_alloc(hwrt, &rt->raa,
			    c->layers * sizeof(uint32_t), 8);
			if (error != 0)
				goto fail;
			be->sync(be->cookie, &rt->srtc,
			    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
			be->sync(be->cookie, &rt->raa,
			    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
		}
		error = pvr_hwrt_alloc(hwrt, &rt->data, sizeof(*fw), 64);
		if (error != 0)
			goto fail;
		fw = rt->data.cpu;
		fw->hwrt_data_common_fw_addr = hwrt->common.address;
		fw->freelists_fw_addr[0] = c->freelist_fw_addr[0];
		fw->freelists_fw_addr[1] = c->freelist_fw_addr[1];
		fw->tail_ptrs_dev_addr = c->tpc_dev_addr;
		fw->vheap_table_dev_addr = c->vheap_table_dev_addr;
		fw->rtc_dev_addr = c->rtc_dev_addr;
		fw->pm_mlist_dev_addr = c->rt[i].pm_mlist_dev_addr;
		fw->macrotile_array_dev_addr = c->rt[i].macrotile_array_dev_addr;
		fw->rgn_header_dev_addr = c->rt[i].region_header_dev_addr;
		fw->rta_ctl.max_rts = c->layers;
		fw->rta_ctl.valid_render_targets_fw_addr = rt->srtc.address;
		fw->rta_ctl.rta_num_partial_renders_fw_addr = rt->raa.address;
		be->sync(be->cookie, &rt->data,
		    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	}
	hwrt->ready = true;
	return 0;
fail:
	(void)pvr_hwrt_destroy(owner);
	return error;
}

int
pvr_hwrt_target(struct pvr_hwrt *hwrt, unsigned index,
    struct pvr_hwrt_target *target)
{
	struct pvr_hwrt_rt *rt;

	if (hwrt == NULL || target == NULL || index >= __arraycount(hwrt->rt))
		return EINVAL;
	if (!hwrt->ready || hwrt->destroying || hwrt->failed)
		return EIO;
	rt = &hwrt->rt[index];
	target->fw_address = rt->data.address;
	target->cleanup_address = rt->data.address +
	    offsetof(struct rogue_fwif_hwrtdata, cleanup_state);
	rt->exposed = true;
	return 0;
}

int
pvr_hwrt_destroy(struct pvr_hwrt **owner)
{
	struct pvr_hwrt *hwrt;
	struct pvr_hwrt_rt *rt;
	struct pvr_fw_object *objects[7];
	const struct pvr_backend *be;
	struct rogue_fwif_kccb_cmd command;
	uint32_t result;
	unsigned i, count = 0;
	int error;

	if (owner == NULL || *owner == NULL)
		return EINVAL;
	hwrt = *owner;
	be = hwrt->backend;
	hwrt->destroying = true;
	if (hwrt->failed)
		return EIO;
	error = be->check(be->cookie);
	if (error != 0)
		return error;
	/* Acknowledge every exposed RT before releasing any shared object. */
	for (i = __arraycount(hwrt->rt); i-- > 0;) {
		rt = &hwrt->rt[i];
		if (!rt->exposed || rt->cleaned)
			continue;
		memset(&command, 0, sizeof(command));
		command.cmd_type = ROGUE_FWIF_KCCB_CMD_CLEANUP;
		command.cmd_data.cleanup_data.cleanup_type = ROGUE_FWIF_CLEANUP_HWRTDATA;
		command.cmd_data.cleanup_data.cleanup_data.hwrt_data_fw_addr = rt->data.address;
		result = 0;
		error = be->command(be->cookie, &command, &result);
		if (error != 0)
			return error;
		if (result == (ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED |
		    ROGUE_FWIF_KCCB_RTN_SLOT_CLEANUP_BUSY))
			return EBUSY;
		if (result != ROGUE_FWIF_KCCB_RTN_SLOT_CMD_EXECUTED) {
			hwrt->failed = true;
			be->fail(be->cookie);
			return EPROTO;
		}
		rt->cleaned = true;
	}
	for (i = __arraycount(hwrt->rt); i-- > 0;) {
		objects[count++] = &hwrt->rt[i].data;
		objects[count++] = &hwrt->rt[i].raa;
		objects[count++] = &hwrt->rt[i].srtc;
	}
	objects[count++] = &hwrt->common;
	for (i = 0; i < count; i++) {
		if (objects[i]->owner == NULL)
			continue;
		error = be->free(be->cookie, objects[i]);
		if (error != 0)
			return error;
	}
	kmem_free(hwrt, sizeof(*hwrt));
	*owner = NULL;
	return 0;
}
