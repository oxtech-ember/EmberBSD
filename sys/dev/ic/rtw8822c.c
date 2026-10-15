/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "rtw8822c.h"

int
rtw8822c_efuse_decode(const uint8_t *data, size_t size,
    struct rtw8822c_efuse *out)
{
	struct rtw8822c_efuse decoded = { 0 };
	size_t cursor = 0, limit, offset;
	unsigned int block, words, word, i;
	uint8_t nonzero = 0;

	if (data == NULL || out == NULL || size == 0 ||
	    size > RTW8822C_EFUSE_PHYSICAL_SIZE)
		return EINVAL;
	limit = RTW8822C_EFUSE_PHYSICAL_SIZE -
	    RTW8822C_EFUSE_PROTECTED_SIZE;
	if (size < limit)
		limit = size;
	memset(decoded.logical, 0xff, sizeof(decoded.logical));
	while (cursor < limit) {
		uint8_t header;

		header = data[cursor++];
		if (header == 0xff)
			break;
		if ((header & 0x1f) == 0x0f) {
			uint8_t extended;

			if (cursor == limit)
				return EINVAL;
			extended = data[cursor++];
			if (extended == 0xff)
				break;
			block = ((extended & 0xf0) >> 1) |
			    ((header >> 5) & 0x07);
			words = extended & 0x0f;
		} else {
			block = header >> 4;
			words = header & 0x0f;
		}
		for (word = 0; word < 4; word++) {
			if (words & (1U << word))
				continue;
			/* Block is at most 127; this multiplication cannot wrap. */
			offset = block * 8 + word * 2;
			if (offset > sizeof(decoded.logical) - 2)
				return EOVERFLOW;
			if (limit - cursor < 2)
				return EINVAL;
			decoded.logical[offset] = data[cursor++];
			decoded.logical[offset + 1] = data[cursor++];
		}
	}

	/* PCIe identity is at 0x120; USB and SDIO have different layouts. */
	memcpy(decoded.mac, decoded.logical + 0x120, sizeof(decoded.mac));
	if (decoded.mac[0] & 1)
		return EINVAL;
	for (i = 0; i < sizeof(decoded.mac); i++)
		nonzero |= decoded.mac[i];
	if (nonzero == 0)
		return EINVAL;

	/* Preserve signed-nibble power data verbatim for the later PHY policy. */
	memcpy(decoded.txpower, decoded.logical + 0x10,
	    sizeof(decoded.txpower));
	decoded.channel_plan = decoded.logical[0xb8];
	decoded.xtal = decoded.logical[0xb9] & 0x7f;
	decoded.board_option = decoded.logical[0xc1];
	decoded.bt_setting = decoded.logical[0xc3];
	decoded.power_track_type = decoded.logical[0xc8] >> 4;
	decoded.rfe_option = decoded.logical[0xca];
	memcpy(decoded.country, decoded.logical + 0xcb, sizeof(decoded.country));
	memcpy(decoded.thermal, decoded.logical + 0xd0, sizeof(decoded.thermal));
	*out = decoded;
	return 0;
}

int
rtw8822c_validate_identity(const struct rtw8822c_identity *id)
{
	unsigned int cut;

	if (id == NULL)
		return EINVAL;
	if (id->vendor != 0x10ec || id->product != 0xc822)
		return ENOTSUP;
	/* BIT_RTL_ID selects test silicon; no test profile is prepared. */
	if (id->sys_cfg1 & UINT32_C(0x00800000))
		return ENOTSUP;
	cut = (id->sys_cfg1 >> 12) & 0x0f;
	/* A-G map to bits 1-7 of the upstream eight-bit power-sequence mask. */
	if (cut > 6 || id->rfe_option > 6)
		return ENOTSUP;
	return 0;
}
