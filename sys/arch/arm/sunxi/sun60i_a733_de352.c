/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Original register-data serialization.
 * Hardware facts: pinned BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f.
 * No GPL implementation is copied. See ember/boot/a733-de352-format.md.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_de352.h>

int
sun60i_a733_de352_rgb_encode(const struct sun60i_a733_de352_rgb *in,
    struct sun60i_a733_de352_rgb_words *out)
{
	const uint64_t limit = UINT64_C(1) << 40;
	struct sun60i_a733_de352_rgb_words words = { 0 };
	uint64_t row, needed;

	if (in == NULL || out == NULL)
		return EINVAL;
	if (in->width == 0 || in->width > 8192 ||
	    in->height == 0 || in->height > 8192)
		return EINVAL;
	row = (uint64_t)in->width * 4;
	if (in->pitch < row || (in->pitch & 3) != 0 || (in->dma & 3) != 0)
		return EINVAL;
	/* Bounded width/height and 32-bit pitch cannot overflow this product. */
	needed = (uint64_t)in->height * in->pitch;
	if (needed > in->mapped_bytes || in->dma >= limit ||
	    needed > limit - in->dma)
		return ERANGE;
	/* XRGB8888, opaque global alpha, straight RGB, progressive layer zero. */
	words.attribute = UINT32_C(0xff000403);
	words.size = (in->width - 1) | ((in->height - 1) << 16);
	words.pitch = in->pitch;
	words.address_low = (uint32_t)in->dma;
	words.address_high_byte = (uint32_t)(in->dma >> 32);
	words.channel_size = words.size;
	words.required_bytes = needed;
	*out = words;
	return 0;
}

int
sun60i_a733_de352_rcq_encode(const struct sun60i_a733_de352_rcq_block *in,
    uint8_t out[A733_DE352_RCQ_DESCRIPTOR_BYTES])
{
	const uint64_t limit = UINT64_C(1) << 40;
	const uint32_t aperture = UINT32_C(0x400000);
	uint32_t words[4];
	uint8_t bytes[A733_DE352_RCQ_DESCRIPTOR_BYTES];

	if (in == NULL || out == NULL)
		return EINVAL;
	if (in->bytes == 0 || in->bytes > UINT32_C(0xffffff) ||
	    (in->bytes & 3) != 0 || (in->destination & 3) != 0 ||
	    (in->dma & 31) != 0)
		return EINVAL;
	if (in->dma >= limit || in->bytes > limit - in->dma ||
	    in->destination >= aperture || in->bytes > aperture - in->destination)
		return ERANGE;
	words[0] = (uint32_t)in->dma;
	words[1] = in->bytes | ((uint32_t)(in->dma >> 32) << 24);
	words[2] = in->dirty ? 1 : 0;
	words[3] = in->destination;
	/* Explicit wire order. Do not depend on host endianness or C bitfields. */
	for (u_int word = 0; word < __arraycount(words); word++) {
		for (u_int byte = 0; byte < 4; byte++)
			bytes[word * 4 + byte] = (uint8_t)(words[word] >> (byte * 8));
	}
	memcpy(out, bytes, sizeof(bytes));
	return 0;
}

int
sun60i_a733_de352_rcq_list_size(uint32_t count, uint32_t *out)
{
	uint64_t even, bytes;

	if (count == 0 || out == NULL)
		return EINVAL;
	even = ((uint64_t)count + 1) & ~UINT64_C(1);
	bytes = even * A733_DE352_RCQ_DESCRIPTOR_BYTES;
	if (bytes > UINT32_MAX)
		return EOVERFLOW;
	*out = (uint32_t)bytes;
	return 0;
}
