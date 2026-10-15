/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88. */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#else
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
rtw88_fw_parse(const uint8_t *data, size_t size, struct rtw88_fw_info *out)
{
	struct rtw88_fw_info info = { 0 };
	size_t cursor = RTW88_FW_HEADER_SIZE;
	uint32_t emem_size;
	int error;

	if (data == NULL || out == NULL || size < RTW88_FW_HEADER_SIZE)
		return EINVAL;
	info.signature = rtw88_get16(data);
	/* This signature is common to B and C, not a chip identifier. */
	if (info.signature != 0x8822)
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

	/*
	 * Fail closed to the evidenced 9.9.11 load profile. Exact bases and
	 * payload lengths bound every destination, including its checksum.
	 * Raw address tags must match too. Future layouts need new evidence.
	 */
	if ((data[24] & 0x10) == 0 ||
	    rtw88_get32(data + 32) != UINT32_C(0x80200000) ||
	    rtw88_get32(data + 60) != UINT32_C(0x80030000) ||
	    rtw88_get32(data + 56) != UINT32_C(0x80100000) ||
	    info.dmem.payload_size != 19328 ||
	    info.imem.payload_size != 54064 ||
	    info.emem.payload_size != 128976)
		return ENOTSUP;

	info.version = rtw88_get16(data + 4);
	info.subversion = data[6];
	info.subindex = data[7];
	info.h2c_version = rtw88_get16(data + 28);
	info.feature = rtw88_get32(data + 12);
	*out = info;
	return 0;
}
