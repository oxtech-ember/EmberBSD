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
 * Register definitions and MIPS startup sequence: Linux PowerVR,
 * 7b63ef2d55f24519e7e9e5f4d15dbea03f126e40, MIT license option.
 * Native fault-aware startup: EmberBSD, AI-assisted.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_gpu_fwstart.h>

/* Exact upstream register constants; no unrelated register map is imported. */
#define ROGUE_CR_SYS_BUS_SECURE 0xA100U
#define ROGUE_CR_SOFT_RESET 0x0100U
#define ROGUE_CR_SOFT_RESET__PBE2_XE__MASKFULL 0xFFEFFFFFFFFFFC3DULL
#define ROGUE_CR_SOFT_RESET_GARTEN_EN 0x0000000100000000ULL
#define ROGUE_CR_SLC_CTRL_MISC 0x3800U
#define ROGUE_CR_SLC_CTRL_MISC_ENABLE_PSG_HAZARD_CHECK_EN 0x0000000000000002ULL
#define ROGUE_CR_SLC_CTRL_MISC_ADDR_DECODE_MODE_PVR_HASH1 0x0000000000200000ULL
#define ROGUE_CR_SLC_CTRL_MISC_BYPASS_BURST_COMBINER_EN 0x0000000000000001ULL
#define ROGUE_CR_SLC_CTRL_MISC_LAZYWB_OVERRIDE_EN 0x0000000001000000ULL
#define ROGUE_CR_MIPS_WRAPPER_CONFIG 0x0810U
#define ROGUE_CR_MIPS_WRAPPER_CONFIG_BOOT_ISA_MODE_MICROMIPS 0x0000000000010000ULL
#define ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG1 0x0818U
#define ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG2 0x0820U
#define ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG1_MODE_ENABLE_EN 0x0000000000000001ULL
#define ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG2_ADDR_OUT_CLRMSK 0xFFFFFF0000000FFFULL
#define ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG1 0x0828U
#define ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG2 0x0830U
#define ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG1 0x0838U
#define ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG2 0x0840U
#define ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG1 0x0858U
#define ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG2 0x0860U
#define ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG 0x0B50U
#define ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META 0x0000000000000000ULL
#define ROGUE_CR_MIPS_DEBUG_CONFIG 0x08C8U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION 0x38C0U
#define ROGUE_CR_SOFT_RESET_RASCAL_CORE_EN 0x0000000080000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_A_CORE_EN 0x0000000020000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_B_CORE_EN 0x0000000040000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_C_CORE_EN 0x0000000800000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_D_CORE_EN 0x0000001000000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_E_CORE_EN 0x0000002000000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_F_CORE_EN 0x0000004000000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_G_CORE_EN 0x0000008000000000ULL
#define ROGUE_CR_SOFT_RESET_DUST_H_CORE_EN 0x0000010000000000ULL
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_AWDOMAIN_NON_SNOOPING_SHIFT 8U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_NON_SNOOPING_SHIFT 10U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_CACHE_MAINTENANCE_SHIFT 16U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_AWDOMAIN_COHERENT_SHIFT 14U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_COHERENT_SHIFT 12U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_AWCACHE_COHERENT_SHIFT 22U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARCACHE_COHERENT_SHIFT 26U
#define ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARCACHE_CACHE_MAINTENANCE_SHIFT 30U

#define RASCAL_DUSTS (ROGUE_CR_SOFT_RESET_RASCAL_CORE_EN | \
    ROGUE_CR_SOFT_RESET_DUST_A_CORE_EN | ROGUE_CR_SOFT_RESET_DUST_B_CORE_EN | \
    ROGUE_CR_SOFT_RESET_DUST_C_CORE_EN | ROGUE_CR_SOFT_RESET_DUST_D_CORE_EN | \
    ROGUE_CR_SOFT_RESET_DUST_E_CORE_EN | ROGUE_CR_SOFT_RESET_DUST_F_CORE_EN | \
    ROGUE_CR_SOFT_RESET_DUST_G_CORE_EN | ROGUE_CR_SOFT_RESET_DUST_H_CORE_EN)

static bool
sun60i_gpu_fwstart_has(const struct sun60i_a733_gpu_fwinfo *info,
    enum sun60i_a733_gpu_fw_feature feature)
{
	uint64_t value;

	return sun60i_a733_gpu_fwinfo_feature(info, feature, &value) == 0;
}

static int
sun60i_gpu_fwstart_write(bus_space_tag_t bst, bus_space_handle_t bsh,
    uint32_t reg, unsigned width, uint64_t value,
    struct sun60i_a733_gpu_fwstart_result *result)
{
	int error;

	result->last_register = reg;
	result->touched = true;
	result->writes++;
	if (width == 4)
		error = bus_space_poke_4(bst, bsh, reg, (uint32_t)value);
	else
		error = bus_space_poke_8(bst, bsh, reg, value);
	if (error != 0)
		return EFAULT;
	bus_space_barrier(bst, bsh, reg, width, BUS_SPACE_BARRIER_WRITE);
	return 0;
}

static int
sun60i_gpu_fwstart_read(bus_space_tag_t bst, bus_space_handle_t bsh,
    uint32_t reg, unsigned width, uint64_t *value,
    struct sun60i_a733_gpu_fwstart_result *result)
{
	uint32_t v32;
	int error;

	result->last_register = reg;
	bus_space_barrier(bst, bsh, reg, width,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	if (width == 4) {
		error = bus_space_peek_4(bst, bsh, reg, &v32);
		if (error == 0)
			*value = v32;
	} else {
		error = bus_space_peek_8(bst, bsh, reg, value);
	}
	return error == 0 ? 0 : EFAULT;
}

#define WRITE(reg, width, value) do { \
	error = sun60i_gpu_fwstart_write(bst, bsh, (reg), (width), (value), result); \
	if (error != 0) return error; \
} while (0)
#define READ(reg, width) do { \
	error = sun60i_gpu_fwstart_read(bst, bsh, (reg), (width), &value, result); \
	if (error != 0) return error; \
} while (0)

int
sun60i_a733_gpu_fwstart(bus_space_tag_t bst, bus_space_handle_t bsh,
    const struct sun60i_a733_gpu_fwinfo *info, uint64_t boot_code,
    uint64_t boot_data, uint64_t exception_code,
    struct sun60i_a733_gpu_fwstart_result *result)
{
	const uint64_t remap[3] = { boot_code, boot_data, exception_code };
	const uint64_t reset = ROGUE_CR_SOFT_RESET__PBE2_XE__MASKFULL;
	const uint64_t axi =
	    (UINT64_C(3) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_AWDOMAIN_NON_SNOOPING_SHIFT) |
	    (UINT64_C(3) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_NON_SNOOPING_SHIFT) |
	    (UINT64_C(2) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_CACHE_MAINTENANCE_SHIFT) |
	    (UINT64_C(2) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_AWDOMAIN_COHERENT_SHIFT) |
	    (UINT64_C(2) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARDOMAIN_COHERENT_SHIFT) |
	    (UINT64_C(2) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_AWCACHE_COHERENT_SHIFT) |
	    (UINT64_C(2) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARCACHE_COHERENT_SHIFT) |
	    (UINT64_C(2) << ROGUE_CR_AXI_ACE_LITE_CONFIGURATION_ARCACHE_CACHE_MAINTENANCE_SHIFT);
	uint64_t value, width, line_size;
	int error;

	if (result == NULL)
		return EINVAL;
	memset(result, 0, sizeof(*result));
	if (info == NULL)
		return EINVAL;
	if (info->bvnc != UINT64_C(0x00240038006800b7) ||
	    !sun60i_gpu_fwstart_has(info, A733_GPU_FW_FEATURE_MIPS) ||
	    !sun60i_gpu_fwstart_has(info, A733_GPU_FW_FEATURE_PBE2_IN_XE) ||
	    sun60i_gpu_fwstart_has(info, A733_GPU_FW_FEATURE_XE_TPU2) ||
	    sun60i_gpu_fwstart_has(info, A733_GPU_FW_FEATURE_GPU_MULTICORE_SUPPORT) ||
	    (info->brn_mask & (UINT64_C(1) << 9)) == 0)
		return EOPNOTSUPP;
	if (sun60i_a733_gpu_fwinfo_feature(info,
	    A733_GPU_FW_FEATURE_PHYS_BUS_WIDTH, &width) != 0 || width != 36 ||
	    sun60i_a733_gpu_fwinfo_feature(info,
	    A733_GPU_FW_FEATURE_SLC_CACHE_LINE_SIZE_BITS, &line_size) != 0 ||
	    line_size != 512)
		return EOPNOTSUPP;
	for (unsigned i = 0; i < __arraycount(remap); i++) {
		if ((remap[i] & 4095) != 0 || remap[i] >> 36 != 0 ||
		    (remap[i] >= 0x1fc00000 && remap[i] < 0x1fc04000))
			return EINVAL;
		for (unsigned j = 0; j < i; j++)
			if (remap[i] == remap[j])
				return EINVAL;
	}
	if (sun60i_gpu_fwstart_has(info, A733_GPU_FW_FEATURE_SYS_BUS_SECURE_RESET)) {
		WRITE(ROGUE_CR_SYS_BUS_SECURE, 4, 0);
		READ(ROGUE_CR_SYS_BUS_SECURE, 4);
	}
	WRITE(ROGUE_CR_SOFT_RESET, 8, reset);
	READ(ROGUE_CR_SOFT_RESET, 8);
	WRITE(ROGUE_CR_SOFT_RESET, 8, reset ^ RASCAL_DUSTS);
	READ(ROGUE_CR_SOFT_RESET, 8);
	WRITE(ROGUE_CR_SOFT_RESET, 8, ROGUE_CR_SOFT_RESET_GARTEN_EN);
	READ(ROGUE_CR_SOFT_RESET, 8);
	READ(ROGUE_CR_SLC_CTRL_MISC, 4);
	value = (value & ROGUE_CR_SLC_CTRL_MISC_ENABLE_PSG_HAZARD_CHECK_EN) |
	    ROGUE_CR_SLC_CTRL_MISC_ADDR_DECODE_MODE_PVR_HASH1 |
	    ROGUE_CR_SLC_CTRL_MISC_BYPASS_BURST_COMBINER_EN;
	if (info->brn_mask & (UINT64_C(1) << 11))
		value |= ROGUE_CR_SLC_CTRL_MISC_LAZYWB_OVERRIDE_EN;
	WRITE(ROGUE_CR_SLC_CTRL_MISC, 4, value);
	WRITE(ROGUE_CR_MIPS_WRAPPER_CONFIG, 4,
	    (UINT32_C(0xcf800000) >> 16) |
	    ROGUE_CR_MIPS_WRAPPER_CONFIG_BOOT_ISA_MODE_MICROMIPS);
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG1, 8, UINT64_C(0x1fc00001));
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP1_CONFIG2, 8, boot_code | 12);
	/* BRN63553: 36-bit cores need address-zero remapped to boot code. */
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG1, 8, 1);
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP5_CONFIG2, 8, boot_code | 12);
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG1, 8, UINT64_C(0x1fc01001));
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP2_CONFIG2, 8, boot_data | 12);
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG1, 8, UINT64_C(0x1fc02001));
	WRITE(ROGUE_CR_MIPS_ADDR_REMAP3_CONFIG2, 8, exception_code | 12);
	WRITE(ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG, 8,
	    ROGUE_CR_MTS_GARTEN_WRAPPER_CONFIG_IDLE_CTRL_META);
	WRITE(ROGUE_CR_MIPS_DEBUG_CONFIG, 4, 0);
	WRITE(ROGUE_CR_AXI_ACE_LITE_CONFIGURATION, 8, axi);
	delay(3);
	WRITE(ROGUE_CR_SOFT_RESET, 8, 0);
	result->released = true;
	READ(ROGUE_CR_SOFT_RESET, 8);
	delay(3);
	return 0;
}
