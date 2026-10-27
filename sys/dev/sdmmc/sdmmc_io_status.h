/* Origin: EmberBSD checked SDIO response decoding, shared with rtw88. */
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
#ifndef _DEV_SDMMC_SDMMC_IO_STATUS_H_
#define _DEV_SDMMC_SDMMC_IO_STATUS_H_

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/errno.h>
#else
#include <stdint.h>
#include <errno.h>
#endif

/* Raw native R5 response, including its low data byte and current-state bits. */
static inline int
sdmmc_io_r5_error(uint32_t response)
{

	if ((response & 0x8000U) != 0)
		return EILSEQ;
	return (response & 0x4b00U) != 0 ? EIO : 0;
}

#endif /* _DEV_SDMMC_SDMMC_IO_STATUS_H_ */
