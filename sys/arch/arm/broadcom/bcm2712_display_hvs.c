/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Numeric facts: raspberrypi/linux a1a601d4857978d810175365550c4536701df4ed,
 * vc4_plane.c, vc4_hvs.c, vc4_regs.h and vc4_drv.h.
 * No GPL implementation code is copied here.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <arm/broadcom/bcm2712_display_hvsvar.h>

#define HVS_DMA_LIMIT		(UINT64_C(1) << 36)
#define HVS_FRAME_BYTES		8294400U
#define HVS_UPM_BYTES		15616U
#define HVS_UPM_ARENA		262144U

int
bcm2712_hvs_encode_1080p60(const struct bcm2712_hvs_frame *input,
    struct bcm2712_hvs_dlist *out)
{
	struct bcm2712_hvs_frame value;
	struct bcm2712_hvs_dlist result = { .words = {
		(1U << 30) | (9U << 24) | (1U << 15) | (2U << 13) | 7U,
		0,
		0xfffU << 4,
		((1080U - 1U) << 16) | (1920U - 1U),
		0xc0c0c0c0U,
		0,
		0,
		7680U,
		1U << 31,
		1U << 31,
	} };

	if (input == NULL || out == NULL)
		return EINVAL;
	value = *input;
	if (value.variant != BCM2712_HVS_C0 && value.variant != BCM2712_HVS_D0)
		return EOPNOTSUPP;
	if (value.dma_base >= HVS_DMA_LIMIT ||
	    value.dma_span < HVS_FRAME_BYTES ||
	    value.dma_span > HVS_DMA_LIMIT - value.dma_base)
		return EINVAL;
	if (value.upm_offset % 256U != 0 || value.upm_length % 256U != 0 ||
	    value.upm_length < HVS_UPM_BYTES || value.upm_offset >= HVS_UPM_ARENA ||
	    value.upm_length > HVS_UPM_ARENA - value.upm_offset ||
	    value.upm_slot >= 32U)
		return EINVAL;

	if (value.variant == BCM2712_HVS_C0)
		result.words[2] |= 1U << 30;
	else
		result.words[0] |= 3U << 18;
	result.words[5] = ((value.upm_offset / 256U) << 16) |
	    (value.upm_slot << 10) | (uint32_t)(value.dma_base >> 32);
	result.words[6] = (uint32_t)value.dma_base;
	*out = result;
	return 0;
}
