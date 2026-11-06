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

#include "rtw8821c.h"
#include "rtw88_efuse.h"

const struct rtw88_fw_profile rtw8821c_fw_profile = {
	.signature = 0x8821,
	.dmem_address = UINT32_C(0x80200000), .dmem_size = 31976,
	.imem_address = UINT32_C(0x80030000), .imem_size = 63240,
	.emem_address = UINT32_C(0x80100000), .emem_size = 44168
};

int
rtw8821c_efuse_decode(const uint8_t *data, size_t size,
    struct rtw8821c_efuse *out)
{
	struct rtw8821c_efuse decoded = { 0 };
	size_t limit;
	unsigned int i;
	uint8_t nonzero = 0;
	int error;

	if (data == NULL || out == NULL || size == 0 ||
	    size > RTW8821C_EFUSE_PHYSICAL_SIZE)
		return EINVAL;
	limit = size < RTW8821C_EFUSE_USABLE_SIZE ? size :
	    RTW8821C_EFUSE_USABLE_SIZE;
	error = rtw88_efuse_expand(data, limit, decoded.logical,
	    sizeof(decoded.logical));
	if (error != 0)
		return error;
	memcpy(decoded.mac, decoded.logical + 0xd0, sizeof(decoded.mac));
	if ((decoded.mac[0] & 1) != 0)
		return EINVAL;
	for (i = 0; i < sizeof(decoded.mac); i++)
		nonzero |= decoded.mac[i];
	if (nonzero == 0)
		return EINVAL;
	decoded.channel_plan = decoded.logical[0xb8];
	decoded.xtal = decoded.logical[0xb9];
	decoded.thermal = decoded.logical[0xba];
	decoded.pa_type = decoded.logical[0xbc];
	memcpy(decoded.lna_2g, decoded.logical + 0xbd, 2);
	memcpy(decoded.lna_5g, decoded.logical + 0xbf, 2);
	decoded.board_option = decoded.logical[0xc1];
	decoded.bt_setting = decoded.logical[0xc3];
	decoded.rfe_option = decoded.logical[0xca] & 0x1f;
	decoded.package_type = (decoded.logical[0xca] >> 5) & 1;
	memcpy(decoded.country, decoded.logical + 0xcb, 2);
	*out = decoded;
	return 0;
}

int
rtw8821c_validate_identity(const struct rtw8821c_identity *id)
{
	if (id == NULL)
		return EINVAL;
	if (id->vendor != 0x10ec || id->product != 0xc821 ||
	    (id->sys_cfg1 & UINT32_C(0x00800000)) != 0)
		return ENOTSUP;
	return 0;
}
