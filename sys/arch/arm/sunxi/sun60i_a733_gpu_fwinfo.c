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
 * Wire definitions and feature parameter mapping: Linux PowerVR driver,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native bounded parser: EmberBSD, AI-assisted. See a733-gpu-fwinfo.md.
 */

#include "sun60i_a733_gpu_fwinfo.h"
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

/* Values follow the set parameter features in ascending feature ID order. */
static const uint8_t parameter_feature[A733_GPU_FW_FEATURE_MAX] = {
	[A733_GPU_FW_FEATURE_CDM_CONTROL_STREAM_FORMAT] = 1,
	[A733_GPU_FW_FEATURE_COMMON_STORE_SIZE_IN_DWORDS] = 1,
	[A733_GPU_FW_FEATURE_ECC_RAMS] = 1,
	[A733_GPU_FW_FEATURE_FBCDC] = 1,
	[A733_GPU_FW_FEATURE_FBCDC_ALGORITHM] = 1,
	[A733_GPU_FW_FEATURE_FBCDC_ARCHITECTURE] = 1,
	[A733_GPU_FW_FEATURE_FBC_MAX_DEFAULT_DESCRIPTORS] = 1,
	[A733_GPU_FW_FEATURE_FBC_MAX_LARGE_DESCRIPTORS] = 1,
	[A733_GPU_FW_FEATURE_ISP_MAX_TILES_IN_FLIGHT] = 1,
	[A733_GPU_FW_FEATURE_ISP_SAMPLES_PER_PIXEL] = 1,
	[A733_GPU_FW_FEATURE_LAYOUT_MARS] = 1,
	[A733_GPU_FW_FEATURE_MAX_PARTITIONS] = 1,
	[A733_GPU_FW_FEATURE_META] = 1,
	[A733_GPU_FW_FEATURE_META_COREMEM_SIZE] = 1,
	[A733_GPU_FW_FEATURE_NUM_CLUSTERS] = 1,
	[A733_GPU_FW_FEATURE_NUM_ISP_IPP_PIPES] = 1,
	[A733_GPU_FW_FEATURE_NUM_OSIDS] = 1,
	[A733_GPU_FW_FEATURE_NUM_RASTER_PIPES] = 1,
	[A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH] = 1,
	[A733_GPU_FW_FEATURE_SIMPLE_PARAMETER_FORMAT_VERSION] = 1,
	[A733_GPU_FW_FEATURE_SLC_BANKS] = 1,
	[A733_GPU_FW_FEATURE_SLC_CACHE_LINE_SIZE_BITS] = 1,
	[A733_GPU_FW_FEATURE_SLC_SIZE_IN_KILOBYTES] = 1,
	[A733_GPU_FW_FEATURE_TILE_SIZE_X] = 1,
	[A733_GPU_FW_FEATURE_TILE_SIZE_Y] = 1,
	[A733_GPU_FW_FEATURE_USC_MIN_OUTPUT_REGISTERS_PER_PIX] = 1,
	[A733_GPU_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS] = 1,
	[A733_GPU_FW_FEATURE_XE_ARCHITECTURE] = 1,
	[A733_GPU_FW_FEATURE_XPU_MAX_REGBANKS_ADDR_WIDTH] = 1,
	[A733_GPU_FW_FEATURE_XPU_MAX_SLAVES] = 1,
	[A733_GPU_FW_FEATURE_XPU_REGISTER_BROADCAST] = 1,
};

static uint32_t
fw_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t
fw_u64(const uint8_t *p)
{
	return (uint64_t)fw_u32(p) | (uint64_t)fw_u32(p + 4) << 32;
}

static int
fw_present(const struct sun60i_a733_gpu_fwinfo *info, unsigned int bit)
{
	return (info->feature_mask[bit / 64] &
	    (UINT64_C(1) << (bit % 64))) != 0;
}

int
sun60i_a733_gpu_fwinfo_feature(const struct sun60i_a733_gpu_fwinfo *info,
    enum sun60i_a733_gpu_fw_feature feature, uint64_t *value)
{
	if (info == NULL || value == NULL ||
	    (unsigned int)feature >= A733_GPU_FW_FEATURE_MAX)
		return EINVAL;
	if (!fw_present(info, (unsigned int)feature))
		return ENOENT;
	*value = info->feature_value[feature];
	return 0;
}

static int
fw_device_info(const uint8_t *bytes, struct sun60i_a733_gpu_fwinfo *info)
{
	const size_t start = info->device_info_offset;
	size_t remaining = info->device_info_size - 32;
	size_t cursor = start + 32;
	size_t param = 0;

	for (unsigned int n = 0; n < A733_GPU_FW_ARRAYS; n++) {
		const uint64_t count = fw_u64(bytes + start + 8 * n);

		/* Divide the known byte bound before multiplication or conversion. */
		if (count > remaining / 8)
			return EINVAL;
		info->array[n].offset = cursor;
		info->array[n].count = (size_t)count;
		cursor += (size_t)count * 8;
		remaining -= (size_t)count * 8;
	}
	if (info->array[A733_GPU_FW_BRN].count > 1 ||
	    info->array[A733_GPU_FW_ERN].count > 1 ||
	    info->array[A733_GPU_FW_FEATURE_MASK].count > 2 ||
	    info->array[A733_GPU_FW_FEATURE_PARAMS].count > 31)
		return EOPNOTSUPP;
	if (info->array[A733_GPU_FW_BRN].count != 0)
		info->brn_mask = fw_u64(bytes + info->array[A733_GPU_FW_BRN].offset);
	if (info->array[A733_GPU_FW_ERN].count != 0)
		info->ern_mask = fw_u64(bytes + info->array[A733_GPU_FW_ERN].offset);
	for (size_t n = 0; n < info->array[A733_GPU_FW_FEATURE_MASK].count; n++)
		info->feature_mask[n] = fw_u64(bytes +
		    info->array[A733_GPU_FW_FEATURE_MASK].offset + 8 * n);
	/* This implementation knows 12 BRNs, 8 ERNs, and 71 feature bits. */
	if ((info->brn_mask >> 12) != 0 || (info->ern_mask >> 8) != 0 ||
	    (info->feature_mask[1] >> (A733_GPU_FW_FEATURE_MAX - 64)) != 0)
		return EOPNOTSUPP;
	if (!fw_present(info, A733_GPU_FW_FEATURE_MIPS) ||
	    fw_present(info, A733_GPU_FW_FEATURE_META) ||
	    fw_present(info, A733_GPU_FW_FEATURE_RISCV_FW_PROCESSOR))
		return EOPNOTSUPP;
	for (unsigned int n = 0; n < A733_GPU_FW_FEATURE_MAX; n++) {
		if (!fw_present(info, n))
			continue;
		if (parameter_feature[n] == 0) {
			info->feature_value[n] = 1;
			continue;
		}
		if (param >= info->array[A733_GPU_FW_FEATURE_PARAMS].count)
			return EINVAL;
		info->feature_value[n] = fw_u64(bytes +
		    info->array[A733_GPU_FW_FEATURE_PARAMS].offset + 8 * param);
		param++;
	}
	if (param != info->array[A733_GPU_FW_FEATURE_PARAMS].count)
		return EOPNOTSUPP;
	return 0;
}

static int
fw_overlaps(uint32_t a, uint32_t a_size, uint32_t b, uint32_t b_size)
{
	/* Each nonempty range has already passed the 32-bit overflow check. */
	return a < b + b_size && b < a + a_size;
}

static int
fw_layouts(const uint8_t *bytes, struct sun60i_a733_gpu_fwinfo *info)
{
	for (unsigned int n = 0; n < info->layout_count; n++) {
		struct sun60i_a733_gpu_fw_layout *entry = &info->layout[n];
		const uint8_t *p = bytes + info->info_offset + 48 + n * 24;
		uint32_t type;

		entry->id = fw_u32(p);
		entry->type = fw_u32(p + 4);
		entry->base_addr = fw_u32(p + 8);
		entry->max_size = fw_u32(p + 12);
		entry->alloc_size = fw_u32(p + 16);
		entry->alloc_offset = fw_u32(p + 20);
		if (entry->id > 14)
			return EINVAL;
		if (entry->id < A733_GPU_FW_MIPS_CODE ||
		    entry->id > A733_GPU_FW_MIPS_STACK)
			return EOPNOTSUPP;
		type = entry->id <= A733_GPU_FW_MIPS_BOOT_CODE ?
		    A733_GPU_FW_CODE : A733_GPU_FW_DATA;
		if (entry->type != type || entry->alloc_size == 0 ||
		    entry->alloc_size > entry->max_size ||
		    entry->max_size > UINT32_MAX - entry->base_addr ||
		    entry->alloc_size > UINT32_MAX - entry->alloc_offset ||
		    entry->alloc_size > UINT32_MAX - info->allocation_size[type])
			return EINVAL;
		info->allocation_size[type] += entry->alloc_size;
		for (unsigned int j = 0; j < n; j++) {
			const struct sun60i_a733_gpu_fw_layout *prev = &info->layout[j];

			if (entry->id == prev->id ||
			    fw_overlaps(entry->base_addr, entry->alloc_size,
			    prev->base_addr, prev->alloc_size) ||
			    (type == prev->type &&
			    fw_overlaps(entry->alloc_offset, entry->alloc_size,
			    prev->alloc_offset, prev->alloc_size)))
				return EINVAL;
		}
	}
	/* Consumers allocate the sum per type, not the largest offset. */
	for (unsigned int n = 0; n < info->layout_count; n++) {
		const struct sun60i_a733_gpu_fw_layout *entry = &info->layout[n];

		if (entry->alloc_offset + entry->alloc_size >
		    info->allocation_size[entry->type])
			return EINVAL;
	}
	return 0;
}

int
sun60i_a733_gpu_fwinfo_parse(const void *data, size_t size,
    uint64_t expected_bvnc, struct sun60i_a733_gpu_fwinfo *out)
{
	struct sun60i_a733_gpu_fwinfo info = { 0 };
	const uint8_t *bytes = data;
	const uint8_t *header;
	uint32_t version;
	int error;

	if (data == NULL || out == NULL || expected_bvnc == 0 ||
	    size < A733_GPU_FW_BLOCK_SIZE || size % A733_GPU_FW_BLOCK_SIZE != 0)
		return EINVAL;
	info.info_offset = size - A733_GPU_FW_BLOCK_SIZE;
	header = bytes + info.info_offset;
	version = fw_u32(header);
	if (version != 3)
		return EOPNOTSUPP;
	info.layout_count = fw_u32(header + 8);
	if (fw_u32(header + 4) != 48 || fw_u32(header + 12) != 24 ||
	    info.layout_count == 0 || info.layout_count > A733_GPU_FW_MAX_LAYOUTS)
		return EINVAL;
	info.bvnc = fw_u64(header + 16);
	info.page_size = fw_u32(header + 24);
	info.flags = fw_u32(header + 28);
	version = fw_u32(header + 32);
	info.version_major = version & 0xffff;
	info.version_minor = version >> 16;
	info.version_build = fw_u32(header + 36);
	info.device_info_size = fw_u32(header + 40);
	if ((info.flags & 1) == 0 || info.version_major != 1 ||
	    info.page_size != A733_GPU_FW_BLOCK_SIZE)
		return EOPNOTSUPP;
	if (info.bvnc != expected_bvnc)
		return ENODEV;
	if (info.device_info_size < 32 || info.device_info_size >= info.info_offset)
		return EINVAL;
	info.device_info_offset = info.info_offset - info.device_info_size;
	info.payload_size = info.device_info_offset;
	/* Fixed header plus at most eight fixed entries fits in the final page. */
	error = fw_layouts(bytes, &info);
	if (error != 0)
		return error;
	error = fw_device_info(bytes, &info);
	if (error != 0)
		return error;
	*out = info;
	return 0;
}
