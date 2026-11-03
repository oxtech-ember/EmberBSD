/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a,
 * rtw88/rtw8723x.h, rtw8723x.c and rtw8723d.c; native code AI-assisted. */
#ifdef _KERNEL
#include <sys/systm.h>
#endif
#include <dev/ic/rtw8723d.h>
#include <dev/ic/rtw88_efuse.h>

int
rtw8723d_efuse_decode(const uint8_t *data, size_t size,
    struct rtw8723d_efuse *out)
{
	struct rtw8723d_efuse decoded;
	uint8_t any = 0;
	unsigned i;
	int error;

	if (data == NULL || out == NULL || size == 0 ||
	    size > RTW8723D_EFUSE_PHYSICAL_SIZE)
		return EINVAL;
	memset(&decoded, 0, sizeof(decoded));
	/* Linux main.c selects chip->ptct_efuse_size (97 for RTL8723D).
	 * Protected bytes cannot supply headers or complete a usable word. */
	if (size > RTW8723D_EFUSE_USABLE_SIZE)
		size = RTW8723D_EFUSE_USABLE_SIZE;
	error = rtw88_efuse_expand(data, size, decoded.logical,
	    sizeof(decoded.logical));
	if (error != 0)
		return error;
	memcpy(decoded.mac, decoded.logical + RTW8723D_EFUSE_SDIO_MAC,
	    sizeof(decoded.mac));
	if ((decoded.mac[0] & 1) != 0)
		return EINVAL;
	for (i = 0; i < sizeof(decoded.mac); i++)
		any |= decoded.mac[i];
	if (any == 0)
		return EINVAL;
	/* Linux rtw8723x_read_efuse selects RFE table 0 for RTL8723D.
	 * Keep the raw map unchanged; it is not the runtime selector. */
	decoded.rfe = 0;
	memcpy(decoded.txpower, decoded.logical + 0x10, sizeof(decoded.txpower));
	decoded.afe = decoded.logical[0x04];
	decoded.channel_plan = decoded.logical[0xb8];
	decoded.xtal = decoded.logical[0xb9];
	decoded.thermal = decoded.logical[0xba];
	decoded.iqk_lck = decoded.logical[0xbb];
	decoded.pa_type = decoded.logical[0xbc];
	memcpy(decoded.lna_2g, decoded.logical + 0xbd, sizeof(decoded.lna_2g));
	decoded.board_option = decoded.logical[0xc1];
	decoded.feature_option = decoded.logical[0xc2];
	decoded.bt_setting = decoded.logical[0xc3];
	memcpy(decoded.country, decoded.logical + 0xcb, sizeof(decoded.country));
	*out = decoded;
	return 0;
}
