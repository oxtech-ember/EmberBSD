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

#ifndef _RTL8723DS_READ_H_
#define _RTL8723DS_READ_H_

#include <sys/types.h>
#ifndef _KERNEL
#include <stdbool.h>
#include <stdint.h>
#endif

struct rtl8723ds_read_ops {
	int (*command)(void *, uint32_t, uint32_t *);
	void *cookie;
};

bool rtl8723ds_read_match(const void *, int, uint16_t, uint16_t, int, int);
int rtl8723ds_read8(const struct rtl8723ds_read_ops *, unsigned int,
    uint32_t, uint8_t *);
int rtl8723ds_read32(const struct rtl8723ds_read_ops *, uint32_t, uint32_t *);

#endif
