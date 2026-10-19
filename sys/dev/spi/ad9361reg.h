/*
 * Copyright 2014-2015(c) Analog Devices, Inc.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of Analog Devices, Inc. nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES, INC. "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL ANALOG DEVICES, INC. BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
 * OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Origin: Definitions from ADI no-OS ad9361.h at ce7107f957d19858f2dffc85bf3d7f2c70367000. */

#ifndef _DEV_SPI_AD9361REG_H_
#define _DEV_SPI_AD9361REG_H_

#define AD9361_SPI_ADDRESS_MASK	0x3ff
#define AD9361_REG_TX_ENABLE	0x002
#define AD9361_REG_RX_ENABLE	0x003
#define AD9361_REG_ENSM_MODE	0x013
#define AD9361_REG_ENSM_CONFIG1	0x014
#define AD9361_REG_ENSM_CONFIG2	0x015
#define AD9361_REG_STATE		0x017
#define AD9361_REG_PRODUCT_ID	0x037
#define AD9361_PRODUCT_ID_MASK	0xf8
#define AD9361_PRODUCT_ID_9361	0x08
#define AD9361_REG_TX_SYNTH_PD	0x051
#define AD9361_REG_TX1_ATTEN0	0x073
#define AD9361_REG_TX1_ATTEN1	0x074
#define AD9361_REG_TX2_ATTEN0	0x075
#define AD9361_REG_TX2_ATTEN1	0x076

#endif
