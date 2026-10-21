/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. All rights reserved. */
/* AI-assisted, independently authored fixed-image interoperability policy. */
#ifndef _DEV_SDMMC_IF_AICWF_FW_BIP_H_
#define _DEV_SDMMC_IF_AICWF_FW_BIP_H_

#include "if_aicwf_fw_bip_data.h"

typedef void (*aicwf_fw_bip_hash_t)(const uint8_t *, size_t, uint8_t[32]);

static inline bool
aicwf_fw_bip_map(uint32_t config, uint32_t desc, uint32_t fw_version,
    uint32_t pairs, const uint32_t options[][2], size_t count)
{
	return config == 0x1730d4 && desc == 0x174000 &&
	    fw_version == 0x06090101 && pairs == 0x179718 && count == 2 &&
	    options[0][0] == 0xb4 && options[0][1] == 0xf3010001 &&
	    options[1][0] == 0x188 && options[1][1] == 1;
}

/* Only these two exact code windows can be read back by this profile. */
static inline bool
aicwf_fw_bip_word(unsigned n, uint32_t *addr, uint32_t *value)
{
	if (n >= 181)
		return false;
	if (n < 85) {
		*addr = 0x142be0 + 4 * n;
		*value = le32dec(aicwf_fw_bip_key + 4 * n);
	} else {
		*addr = 0x165e48 + 4 * (n - 85);
		*value = le32dec(aicwf_fw_bip_rx + 4 * (n - 85));
	}
	return true;
}

/* The existing option order and all four disabled copy blocks are fixed. */
static inline bool
aicwf_fw_bip_meta_word(unsigned n, uint32_t *addr, uint32_t *value)
{
	static const uint32_t words[12][2] = {
		{0x174000, 0x48435450}, {0x174008, 0x50544348},
		{0x174004, 0x179718}, {0x17400c, 2},
		{0x179718, 0x173188}, {0x17971c, 0xf3010001},
		{0x179720, 0x17325c}, {0x179724, 1},
		{0x174030, 0}, {0x174034, 0},
		{0x174038, 0}, {0x17403c, 0},
	};

	if (n >= 12)
		return false;
	*addr = words[n][0];
	*value = words[n][1];
	return true;
}

/* The caller owns this RAM buffer and discards it on error. */
static inline int
aicwf_fw_bip_patch(uint8_t *data, size_t size, bool h, bool *pending,
    aicwf_fw_bip_hash_t hash)
{
	uint8_t digest[32];

	*pending = false;
	if (h || size != 341200)
		return 0;
	hash(data, size, digest);
	if (memcmp(digest, aicwf_fw_bip_original_sha256, 32) != 0)
		return 0;
	if (le32dec(data + 0x1c) != 0x06090101 ||
	    le32dec(data + 0x198) != 0x1730d4 ||
	    le32dec(data + 0x1a0) != 0x174000 ||
	    le32dec(data + 0x1a4) != 0x179718)
		return EIO;
	memcpy(data + 0x22be0, aicwf_fw_bip_key, sizeof(aicwf_fw_bip_key));
	memcpy(data + 0x45e48, aicwf_fw_bip_rx, sizeof(aicwf_fw_bip_rx));
	hash(data, size, digest);
	if (memcmp(digest, aicwf_fw_bip_modified_sha256, 32) != 0)
		return EIO;
	*pending = true;
	return 0;
}

#endif
