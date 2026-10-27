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

#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif
#include "rtl8723ds_read.h"
#include "rtw88_sdio_io.h"

int
rtl8723ds_read8(const struct rtl8723ds_read_ops *ops, unsigned int function,
    uint32_t address, uint8_t *value)
{
	struct rtw88_sdio_ops common = { 0 };

	if (ops == NULL)
		return EINVAL;
	common.command52 = ops->command;
	common.cookie = ops->cookie;
	return rtw88_sdio_cmd52(&common, function, address, false, value);
}

int
rtl8723ds_read32(const struct rtl8723ds_read_ops *ops, uint32_t address,
    uint32_t *value)
{
	struct rtw88_sdio_ops common = { 0 };
	uint8_t bytes[4], scratch[4];
	struct rtw88_sdio_bus bus = {
		.ops = &common, .max_request = 512,
		.read_scratch = scratch, .read_scratch_len = sizeof(scratch)
	};
	enum rtw88_sdio_domain domain;
	int error;

	if (ops == NULL || value == NULL)
		return EINVAL;
	switch (address & 0xffff0000U) {
	case 0x10250000U:
		domain = RTW88_SDIO_LOCAL;
		break;
	case 0x10260000U:
		domain = RTW88_SDIO_MAC;
		break;
	default:
		return EINVAL;
	}
	common.command52 = ops->command;
	common.cookie = ops->cookie;
	error = rtw88_sdio_transfer(&bus, domain, address & 0xffff,
	    bytes, sizeof(bytes), false);
	if (error != 0)
		return error;
	*value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
	    ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
	return 0;
}
