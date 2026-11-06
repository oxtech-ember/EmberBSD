/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#else
#include <stddef.h>
#include <errno.h>
#endif

#include "rtw88_hw.h"

/* Byte decoding avoids unaligned accesses and host-endian assumptions. */
static uint16_t
rtw88_get16(const uint8_t *p)
{

	return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static uint32_t
rtw88_get32(const uint8_t *p)
{

	return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
	    (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int
rtw88_fw_section(size_t *cursor, size_t size, uint32_t payload,
    uint32_t address, int checksum, struct rtw88_fw_section *section)
{
	uint32_t length;

	if (checksum && payload > UINT32_MAX - RTW88_FW_CHECKSUM_SIZE)
		return EOVERFLOW;
	length = payload + (checksum ? RTW88_FW_CHECKSUM_SIZE : 0);
	if (*cursor > size || length > size - *cursor)
		return EINVAL;
	section->offset = *cursor;
	section->size = length;
	section->payload_size = payload;
	section->load_address = address & UINT32_C(0x7fffffff);
	/* The subtraction above proves this addition cannot wrap size_t. */
	*cursor += length;
	return 0;
}

int
rtw88_fw_parse(const uint8_t *data, size_t size,
    const struct rtw88_fw_profile *profile, struct rtw88_fw_info *out)
{
	struct rtw88_fw_info info = { 0 };
	size_t cursor = RTW88_FW_HEADER_SIZE;
	uint32_t emem_size;
	int error;

	if (data == NULL || out == NULL || profile == NULL ||
	    size < RTW88_FW_HEADER_SIZE)
		return EINVAL;
	info.signature = rtw88_get16(data);
	/* Signature alone does not identify a chip or authenticate firmware. */
	if (info.signature != profile->signature)
		return ENOTSUP;
	error = rtw88_fw_section(&cursor, size, rtw88_get32(data + 36),
	    rtw88_get32(data + 32), 1, &info.dmem);
	if (error != 0)
		return error;
	error = rtw88_fw_section(&cursor, size, rtw88_get32(data + 48),
	    rtw88_get32(data + 60), 1, &info.imem);
	if (error != 0)
		return error;
	emem_size = (data[24] & 0x10) ? rtw88_get32(data + 52) : 0;
	error = rtw88_fw_section(&cursor, size, emem_size,
	    rtw88_get32(data + 56), emem_size != 0, &info.emem);
	if (error != 0)
		return error;
	if (cursor != size)
		return EINVAL;

	/* The chip supplies evidenced raw destinations and payload lengths.
	 * Compare tagged addresses too, after validating every file range. */
	if ((data[24] & 0x10) == 0 ||
	    rtw88_get32(data + 32) != profile->dmem_address ||
	    rtw88_get32(data + 60) != profile->imem_address ||
	    rtw88_get32(data + 56) != profile->emem_address ||
	    info.dmem.payload_size != profile->dmem_size ||
	    info.imem.payload_size != profile->imem_size ||
	    info.emem.payload_size != profile->emem_size)
		return ENOTSUP;

	info.version = rtw88_get16(data + 4);
	info.subversion = data[6];
	info.subindex = data[7];
	info.h2c_version = rtw88_get16(data + 28);
	info.feature = rtw88_get32(data + 12);
	*out = info;
	return 0;
}

/*
 * WME queue and EDCA formats follow FreeBSD rtw88 at
 * d2145e9b96448da34d3cfede24c509af6d830c92, pci.c and mac80211.c.
 * The BSD-3-Clause option applies. No LinuxKPI policy is imported.
 */
int
rtw88_wme_map(unsigned tid, struct rtw88_wme_map *out)
{
	static const uint8_t ac[8] = { 0, 1, 1, 0, 2, 2, 3, 3 };
	static const uint8_t ring[4] = { 1, 0, 2, 3 };
	static const uint32_t edca[4] = { 0x0508, 0x050c, 0x0504, 0x0500 };
	struct rtw88_wme_map map;

	if (out == NULL || tid > 15)
		return EINVAL;
	if (tid > 7)
		return EOPNOTSUPP;
	map.ac = ac[tid];
	map.ring = ring[map.ac];
	map.edca = edca[map.ac];
	map.qsel = (uint8_t)tid;
	*out = map;
	return 0;
}

int
rtw88_edca_encode(const struct rtw88_edca_params p[RTW88_WME_NAC],
    unsigned slot, unsigned sifs, uint32_t out[RTW88_WME_NAC])
{
	uint32_t words[RTW88_WME_NAC];
	unsigned i, aifs;

	if (p == NULL || out == NULL || (slot != 9 && slot != 20) ||
	    (sifs != 10 && sifs != 16))
		return EINVAL;
	for (i = 0; i < RTW88_WME_NAC; i++) {
		if (p[i].aifsn > 15 || p[i].ecw_min > 15 ||
		    p[i].ecw_max > 15 || p[i].ecw_min > p[i].ecw_max ||
		    p[i].txop > 0xffff)
			return EINVAL;
		aifs = p[i].aifsn * slot + sifs;
		if (aifs > 0xff)
			return ERANGE;
		words[i] = (uint32_t)p[i].txop << 16 |
		    (uint32_t)p[i].ecw_max << 12 |
		    (uint32_t)p[i].ecw_min << 8 | aifs;
	}
	for (i = 0; i < RTW88_WME_NAC; i++)
		out[i] = words[i];
	return 0;
}

int
rtw88_dma_validate(uint64_t addr, size_t len, unsigned addr_bits)
{
	uint64_t maximum;

	if (len == 0 || addr_bits == 0 || addr_bits > 64)
		return EINVAL;
	if (addr_bits > 32)
		return ENOTSUP;
	maximum = (UINT64_C(1) << addr_bits) - 1;
	/* Check the last byte without forming a wrapping addr + len. */
	if (addr > maximum || len - 1 > maximum - addr)
		return EOVERFLOW;
	return 0;
}
