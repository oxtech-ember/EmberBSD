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

#ifndef _SUN60I_A733_GPU_FWINFO_H
#define _SUN60I_A733_GPU_FWINFO_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stddef.h>
#include <stdint.h>
#endif

#define A733_GPU_FW_BLOCK_SIZE	4096
#define A733_GPU_FW_MAX_LAYOUTS	8
#define A733_GPU_FW_ARRAYS	4

enum sun60i_a733_gpu_fw_section {
	A733_GPU_FW_MIPS_CODE = 4,
	A733_GPU_FW_MIPS_EXCEPTIONS_CODE = 5,
	A733_GPU_FW_MIPS_BOOT_CODE = 6,
	A733_GPU_FW_MIPS_PRIVATE_DATA = 7,
	A733_GPU_FW_MIPS_BOOT_DATA = 8,
	A733_GPU_FW_MIPS_STACK = 9
};

enum sun60i_a733_gpu_fw_type {
	A733_GPU_FW_CODE = 1,
	A733_GPU_FW_DATA = 2
};

enum sun60i_a733_gpu_fw_array {
	A733_GPU_FW_BRN, A733_GPU_FW_ERN,
	A733_GPU_FW_FEATURE_MASK, A733_GPU_FW_FEATURE_PARAMS
};

enum sun60i_a733_gpu_fw_feature {
	A733_GPU_FW_FEATURE_AXI_ACELITE = 0,
	A733_GPU_FW_FEATURE_CDM_CONTROL_STREAM_FORMAT,
	A733_GPU_FW_FEATURE_CLUSTER_GROUPING,
	A733_GPU_FW_FEATURE_COMMON_STORE_SIZE_IN_DWORDS,
	A733_GPU_FW_FEATURE_COMPUTE,
	A733_GPU_FW_FEATURE_COMPUTE_MORTON_CAPABLE,
	A733_GPU_FW_FEATURE_COMPUTE_OVERLAP,
	A733_GPU_FW_FEATURE_COREID_PER_OS,
	A733_GPU_FW_FEATURE_DYNAMIC_DUST_POWER,
	A733_GPU_FW_FEATURE_ECC_RAMS,
	A733_GPU_FW_FEATURE_FBCDC,
	A733_GPU_FW_FEATURE_FBCDC_ALGORITHM,
	A733_GPU_FW_FEATURE_FBCDC_ARCHITECTURE,
	A733_GPU_FW_FEATURE_FBC_MAX_DEFAULT_DESCRIPTORS,
	A733_GPU_FW_FEATURE_FBC_MAX_LARGE_DESCRIPTORS,
	A733_GPU_FW_FEATURE_FB_CDC_V4,
	A733_GPU_FW_FEATURE_GPU_MULTICORE_SUPPORT,
	A733_GPU_FW_FEATURE_GPU_VIRTUALISATION,
	A733_GPU_FW_FEATURE_GS_RTA_SUPPORT,
	A733_GPU_FW_FEATURE_IRQ_PER_OS,
	A733_GPU_FW_FEATURE_ISP_MAX_TILES_IN_FLIGHT,
	A733_GPU_FW_FEATURE_ISP_SAMPLES_PER_PIXEL,
	A733_GPU_FW_FEATURE_ISP_ZLS_D24_S8_PACKING_OGL_MODE,
	A733_GPU_FW_FEATURE_LAYOUT_MARS,
	A733_GPU_FW_FEATURE_MAX_PARTITIONS,
	A733_GPU_FW_FEATURE_META,
	A733_GPU_FW_FEATURE_META_COREMEM_SIZE,
	A733_GPU_FW_FEATURE_MIPS,
	A733_GPU_FW_FEATURE_NUM_CLUSTERS,
	A733_GPU_FW_FEATURE_NUM_ISP_IPP_PIPES,
	A733_GPU_FW_FEATURE_NUM_OSIDS,
	A733_GPU_FW_FEATURE_NUM_RASTER_PIPES,
	A733_GPU_FW_FEATURE_PBE2_IN_XE,
	A733_GPU_FW_FEATURE_PBVNC_COREID_REG,
	A733_GPU_FW_FEATURE_PERFBUS,
	A733_GPU_FW_FEATURE_PERF_COUNTER_BATCH,
	A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH,
	A733_GPU_FW_FEATURE_RISCV_FW_PROCESSOR,
	A733_GPU_FW_FEATURE_ROGUEXE,
	A733_GPU_FW_FEATURE_S7_TOP_INFRASTRUCTURE,
	A733_GPU_FW_FEATURE_SIMPLE_INTERNAL_PARAMETER_FORMAT,
	A733_GPU_FW_FEATURE_SIMPLE_INTERNAL_PARAMETER_FORMAT_V2,
	A733_GPU_FW_FEATURE_SIMPLE_PARAMETER_FORMAT_VERSION,
	A733_GPU_FW_FEATURE_SLC_BANKS,
	A733_GPU_FW_FEATURE_SLC_CACHE_LINE_SIZE_BITS,
	A733_GPU_FW_FEATURE_SLC_SIZE_CONFIGURABLE,
	A733_GPU_FW_FEATURE_SLC_SIZE_IN_KILOBYTES,
	A733_GPU_FW_FEATURE_SOC_TIMER,
	A733_GPU_FW_FEATURE_SYS_BUS_SECURE_RESET,
	A733_GPU_FW_FEATURE_TESSELLATION,
	A733_GPU_FW_FEATURE_TILE_REGION_PROTECTION,
	A733_GPU_FW_FEATURE_TILE_SIZE_X,
	A733_GPU_FW_FEATURE_TILE_SIZE_Y,
	A733_GPU_FW_FEATURE_TLA,
	A733_GPU_FW_FEATURE_TPU_CEM_DATAMASTER_GLOBAL_REGISTERS,
	A733_GPU_FW_FEATURE_TPU_DM_GLOBAL_REGISTERS,
	A733_GPU_FW_FEATURE_TPU_FILTERING_MODE_CONTROL,
	A733_GPU_FW_FEATURE_USC_MIN_OUTPUT_REGISTERS_PER_PIX,
	A733_GPU_FW_FEATURE_VDM_DRAWINDIRECT,
	A733_GPU_FW_FEATURE_VDM_OBJECT_LEVEL_LLS,
	A733_GPU_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS,
	A733_GPU_FW_FEATURE_WATCHDOG_TIMER,
	A733_GPU_FW_FEATURE_WORKGROUP_PROTECTION,
	A733_GPU_FW_FEATURE_XE_ARCHITECTURE,
	A733_GPU_FW_FEATURE_XE_MEMORY_HIERARCHY,
	A733_GPU_FW_FEATURE_XE_TPU2,
	A733_GPU_FW_FEATURE_XPU_MAX_REGBANKS_ADDR_WIDTH,
	A733_GPU_FW_FEATURE_XPU_MAX_SLAVES,
	A733_GPU_FW_FEATURE_XPU_REGISTER_BROADCAST,
	A733_GPU_FW_FEATURE_XT_TOP_INFRASTRUCTURE,
	A733_GPU_FW_FEATURE_ZLS_SUBTILE,

	A733_GPU_FW_FEATURE_MAX
};

struct sun60i_a733_gpu_fw_layout {
	uint32_t id, type;
	uint32_t base_addr, max_size, alloc_size, alloc_offset;
};

struct sun60i_a733_gpu_fwinfo {
	uint64_t bvnc;
	uint32_t flags, page_size, version_build;
	uint16_t version_major, version_minor;
	size_t payload_size, info_offset, device_info_offset;
	uint32_t device_info_size, layout_count;
	struct sun60i_a733_gpu_fw_layout layout[A733_GPU_FW_MAX_LAYOUTS];
	/* Index 0 is unused; indices 1 and 2 are CODE and DATA. */
	uint32_t allocation_size[3];
	/* Immutable input spans, in bytes and u64 elements respectively. */
	struct {
		size_t offset, count;
	} array[A733_GPU_FW_ARRAYS];
	uint64_t brn_mask, ern_mask, feature_mask[2];
	/* Boolean features are 1; absent features are 0. Check the mask first. */
	uint64_t feature_value[A733_GPU_FW_FEATURE_MAX];
};

/*
 * Validate metadata compatibility for the first MIPS path, not ELF payload,
 * firmware authenticity, or execution. No allocation, I/O, or device access.
 * Supply a stable input buffer and the expected live packed BVNC.
 * On any error, leave out unchanged. Return positive errno values.
 * EINVAL: malformed metadata or arguments. EOPNOTSUPP: unsupported format,
 * processor, page size, version, or feature. ENODEV: mismatched BVNC.
 * The decoded result owns no pointers. Spans refer to the original input.
 */
int sun60i_a733_gpu_fwinfo_parse(const void *, size_t, uint64_t,
    struct sun60i_a733_gpu_fwinfo *);

/* Return a present feature's value, or ENOENT without changing the output. */
int sun60i_a733_gpu_fwinfo_feature(const struct sun60i_a733_gpu_fwinfo *,
    enum sun60i_a733_gpu_fw_feature, uint64_t *);

#endif /* _SUN60I_A733_GPU_FWINFO_H */
