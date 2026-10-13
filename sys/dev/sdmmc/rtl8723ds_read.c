/* Origin: EmberBSD - RTL8723DS reads from Linux v6.12 adc218676eef25575469234709c2d87185ca223a (BSD). */
/* SPDX-License-Identifier: BSD-3-Clause */
/*-
 * Copyright (c) 2021 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 * Copyright (c) 2021 Jernej Skrabec <jernej.skrabec@gmail.com>
 * Copyright (c) 2018-2019 Realtek Corporation
 * Copyright (c) 2026 Anton and EmberBSD contributors
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif
#include <libfdt.h>
#include "rtl8723ds_read.h"

/* rtw88/sdio.h domain mapping; only direct byte reads while MAC is off. */
#define RTL_LOCAL 0x10250000U
#define RTL_MAC 0x10260000U
#define RTL_HOST "/soc@03000000/sdmmc@04021000"

static bool
rtl8723ds_prop(const void *fdt, int node, const char *name,
    const void *want, int size)
{
	const void *p;
	int len;

	p = fdt_getprop(fdt, node, name, &len);
	return p != NULL && len == size && memcmp(p, want, size) == 0;
}

bool
rtl8723ds_read_match(const void *fdt, int host, uint16_t vendor,
    uint16_t product, int function, int interface)
{
	fdt32_t reg[4];

	if (fdt == NULL || fdt_check_header(fdt) != 0 ||
	    vendor != 0x024c || product != 0xd723 || function != 1 ||
	    interface != 7 || host < 0 || host != fdt_path_offset(fdt, RTL_HOST))
		return false;
	if (fdt_stringlist_count(fdt, 0, "compatible") < 1 ||
	    fdt_node_check_compatible(fdt, 0, "allwinner,a133") != 0 ||
	    !rtl8723ds_prop(fdt, 0, "ember,ys-m33-sdio-probe", "", 0) ||
	    !rtl8723ds_prop(fdt, 0, "ember,ys-m33-rtl8723ds-read-probe", "", 0) ||
	    !rtl8723ds_prop(fdt, host, "ember,ys-m33-sdio-host", "", 0) ||
	    !rtl8723ds_prop(fdt, host, "compatible", "allwinner,sun50i-a100-mmc",
	    sizeof("allwinner,sun50i-a100-mmc")) ||
	    !rtl8723ds_prop(fdt, host, "status", "okay", 5))
		return false;
	reg[0] = 0; reg[1] = cpu_to_fdt32(0x04021000);
	reg[2] = 0; reg[3] = cpu_to_fdt32(0x1000);
	return rtl8723ds_prop(fdt, host, "reg", reg, sizeof(reg));
}

int
rtl8723ds_read8(const struct rtl8723ds_read_ops *ops, unsigned int function,
    uint32_t address, uint8_t *value)
{
	uint32_t response, argument;
	int error;

	if (ops == NULL || ops->command == NULL || value == NULL ||
	    function > 1 || address > 0x1ffff)
		return EINVAL;
	/* CMD52 read: no WRITE, RAW or reserved bits, no silent address masking. */
	argument = (function << 28) | (address << 9);
	error = ops->command(ops->cookie, argument, &response);
	if (error != 0)
		return error;
	/* R5 CRC, illegal command, error, bad function and out-of-range flags. */
	if (response & 0x8000)
		return EILSEQ;
	if (response & 0x4b00)
		return EIO;
	*value = response & 0xff;
	return 0;
}

int
rtl8723ds_read32(const struct rtl8723ds_read_ops *ops, uint32_t address,
    uint32_t *value)
{
	uint32_t offset, bus, limit, result = 0;
	uint8_t byte;
	int error;

	if (value == NULL)
		return EINVAL;
	switch (address & 0xffff0000U) {
	case RTL_LOCAL:
		limit = 0xfff;
		bus = 0;
		break;
	case RTL_MAC:
		limit = 0xffff;
		bus = 0x10000;
		break;
	default:
		return EINVAL;
	}
	offset = address & 0xffff;
	if (offset > limit - 3)
		return EINVAL;
	bus |= offset;
	for (unsigned int i = 0; i < 4; i++) {
		error = rtl8723ds_read8(ops, 1, bus + i, &byte);
		if (error != 0)
			return error;
		result |= (uint32_t)byte << (i * 8);
	}
	*value = result;
	return 0;
}
