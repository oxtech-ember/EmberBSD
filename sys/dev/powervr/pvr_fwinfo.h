/* SPDX-License-Identifier: MIT */
/* Origin: EmberBSD PowerVR shared mechanism, from A733; AI-assisted. */
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
 * Native bounded parser: EmberBSD, AI-assisted. See the common PowerVR README.
 */

#ifndef _PVR_FWINFO_H
#define _PVR_FWINFO_H

#include <sys/types.h>
#ifndef _KERNEL
#include <stddef.h>
#include <stdint.h>
#endif

enum pvr_fw_profile { PVR_FW_PROFILE_A733, PVR_FW_PROFILE_GE8300 };

#define PVR_FW_BLOCK_SIZE	4096
#define PVR_FW_MAX_LAYOUTS	8
#define PVR_FW_ARRAYS	4

enum pvr_fw_section {
	PVR_FW_MIPS_CODE = 4,
	PVR_FW_MIPS_EXCEPTIONS_CODE = 5,
	PVR_FW_MIPS_BOOT_CODE = 6,
	PVR_FW_MIPS_PRIVATE_DATA = 7,
	PVR_FW_MIPS_BOOT_DATA = 8,
	PVR_FW_MIPS_STACK = 9
};

enum pvr_fw_type {
	PVR_FW_CODE = 1,
	PVR_FW_DATA = 2
};

enum pvr_fw_array {
	PVR_FW_BRN, PVR_FW_ERN,
	PVR_FW_FEATURE_MASK, PVR_FW_FEATURE_PARAMS
};

enum pvr_fw_feature {
	PVR_FW_FEATURE_AXI_ACELITE = 0,
	PVR_FW_FEATURE_CDM_CONTROL_STREAM_FORMAT,
	PVR_FW_FEATURE_CLUSTER_GROUPING,
	PVR_FW_FEATURE_COMMON_STORE_SIZE_IN_DWORDS,
	PVR_FW_FEATURE_COMPUTE,
	PVR_FW_FEATURE_COMPUTE_MORTON_CAPABLE,
	PVR_FW_FEATURE_COMPUTE_OVERLAP,
	PVR_FW_FEATURE_COREID_PER_OS,
	PVR_FW_FEATURE_DYNAMIC_DUST_POWER,
	PVR_FW_FEATURE_ECC_RAMS,
	PVR_FW_FEATURE_FBCDC,
	PVR_FW_FEATURE_FBCDC_ALGORITHM,
	PVR_FW_FEATURE_FBCDC_ARCHITECTURE,
	PVR_FW_FEATURE_FBC_MAX_DEFAULT_DESCRIPTORS,
	PVR_FW_FEATURE_FBC_MAX_LARGE_DESCRIPTORS,
	PVR_FW_FEATURE_FB_CDC_V4,
	PVR_FW_FEATURE_GPU_MULTICORE_SUPPORT,
	PVR_FW_FEATURE_GPU_VIRTUALISATION,
	PVR_FW_FEATURE_GS_RTA_SUPPORT,
	PVR_FW_FEATURE_IRQ_PER_OS,
	PVR_FW_FEATURE_ISP_MAX_TILES_IN_FLIGHT,
	PVR_FW_FEATURE_ISP_SAMPLES_PER_PIXEL,
	PVR_FW_FEATURE_ISP_ZLS_D24_S8_PACKING_OGL_MODE,
	PVR_FW_FEATURE_LAYOUT_MARS,
	PVR_FW_FEATURE_MAX_PARTITIONS,
	PVR_FW_FEATURE_META,
	PVR_FW_FEATURE_META_COREMEM_SIZE,
	PVR_FW_FEATURE_MIPS,
	PVR_FW_FEATURE_NUM_CLUSTERS,
	PVR_FW_FEATURE_NUM_ISP_IPP_PIPES,
	PVR_FW_FEATURE_NUM_OSIDS,
	PVR_FW_FEATURE_NUM_RASTER_PIPES,
	PVR_FW_FEATURE_PBE2_IN_XE,
	PVR_FW_FEATURE_PBVNC_COREID_REG,
	PVR_FW_FEATURE_PERFBUS,
	PVR_FW_FEATURE_PERF_COUNTER_BATCH,
	PVR_FW_FEATURE_PHYS_BUS_WIDTH,
	PVR_FW_FEATURE_RISCV_FW_PROCESSOR,
	PVR_FW_FEATURE_ROGUEXE,
	PVR_FW_FEATURE_S7_TOP_INFRASTRUCTURE,
	PVR_FW_FEATURE_SIMPLE_INTERNAL_PARAMETER_FORMAT,
	PVR_FW_FEATURE_SIMPLE_INTERNAL_PARAMETER_FORMAT_V2,
	PVR_FW_FEATURE_SIMPLE_PARAMETER_FORMAT_VERSION,
	PVR_FW_FEATURE_SLC_BANKS,
	PVR_FW_FEATURE_SLC_CACHE_LINE_SIZE_BITS,
	PVR_FW_FEATURE_SLC_SIZE_CONFIGURABLE,
	PVR_FW_FEATURE_SLC_SIZE_IN_KILOBYTES,
	PVR_FW_FEATURE_SOC_TIMER,
	PVR_FW_FEATURE_SYS_BUS_SECURE_RESET,
	PVR_FW_FEATURE_TESSELLATION,
	PVR_FW_FEATURE_TILE_REGION_PROTECTION,
	PVR_FW_FEATURE_TILE_SIZE_X,
	PVR_FW_FEATURE_TILE_SIZE_Y,
	PVR_FW_FEATURE_TLA,
	PVR_FW_FEATURE_TPU_CEM_DATAMASTER_GLOBAL_REGISTERS,
	PVR_FW_FEATURE_TPU_DM_GLOBAL_REGISTERS,
	PVR_FW_FEATURE_TPU_FILTERING_MODE_CONTROL,
	PVR_FW_FEATURE_USC_MIN_OUTPUT_REGISTERS_PER_PIX,
	PVR_FW_FEATURE_VDM_DRAWINDIRECT,
	PVR_FW_FEATURE_VDM_OBJECT_LEVEL_LLS,
	PVR_FW_FEATURE_VIRTUAL_ADDRESS_SPACE_BITS,
	PVR_FW_FEATURE_WATCHDOG_TIMER,
	PVR_FW_FEATURE_WORKGROUP_PROTECTION,
	PVR_FW_FEATURE_XE_ARCHITECTURE,
	PVR_FW_FEATURE_XE_MEMORY_HIERARCHY,
	PVR_FW_FEATURE_XE_TPU2,
	PVR_FW_FEATURE_XPU_MAX_REGBANKS_ADDR_WIDTH,
	PVR_FW_FEATURE_XPU_MAX_SLAVES,
	PVR_FW_FEATURE_XPU_REGISTER_BROADCAST,
	PVR_FW_FEATURE_XT_TOP_INFRASTRUCTURE,
	PVR_FW_FEATURE_ZLS_SUBTILE,

	PVR_FW_FEATURE_MAX
};

struct pvr_fw_layout {
	uint32_t id, type;
	uint32_t base_addr, max_size, alloc_size, alloc_offset;
};

struct pvr_fwinfo {
	uint64_t bvnc;
	uint32_t flags, page_size, version_build;
	uint16_t version_major, version_minor;
	size_t payload_size, info_offset, device_info_offset;
	uint32_t device_info_size, layout_count;
	struct pvr_fw_layout layout[PVR_FW_MAX_LAYOUTS];
	/* Index 0 is unused; indices 1 and 2 are CODE and DATA. */
	uint32_t allocation_size[3];
	/* Immutable input spans, in bytes and u64 elements respectively. */
	struct {
		size_t offset, count;
	} array[PVR_FW_ARRAYS];
	uint64_t brn_mask, ern_mask, feature_mask[2];
	/* Boolean features are 1; absent features are 0. Check the mask first. */
	uint64_t feature_value[PVR_FW_FEATURE_MAX];
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
int pvr_fwinfo_parse(const void *, size_t, uint64_t,
    struct pvr_fwinfo *);

/* Return a present feature's value, or ENOENT without changing the output. */
int pvr_fwinfo_feature(const struct pvr_fwinfo *,
    enum pvr_fw_feature, uint64_t *);

#endif /* _PVR_FWINFO_H */
