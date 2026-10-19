/*-
 * Copyright (c) 2018 Thomas Skibo <thomasskibo@yahoo.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Origin: Register definitions from FreeBSD zy7_spi.c at bd369bc7c2368b19789cfabcea99e355e1e959da. */

#ifndef _ARM_XILINX_ZYNQ_SPIREG_H_
#define _ARM_XILINX_ZYNQ_SPIREG_H_

#define ZSPI_CONFIG		0x00
#define  ZSPI_CFG_CS		__BITS(13, 10)
#define  ZSPI_CFG_MAN_CS		__BIT(14)
#define  ZSPI_CFG_BAUD		__BITS(5, 3)
#define  ZSPI_CFG_CPHA		__BIT(2)
#define  ZSPI_CFG_CPOL		__BIT(1)
#define  ZSPI_CFG_MASTER	__BIT(0)
#define ZSPI_STATUS		0x04
#define ZSPI_INT_DISABLE	0x0c
#define  ZSPI_RX_NOT_EMPTY	__BIT(4)
#define  ZSPI_TX_NOT_FULL	__BIT(2)
#define  ZSPI_MODE_FAULT		__BIT(1)
#define  ZSPI_RX_OVERFLOW	__BIT(0)
#define ZSPI_ENABLE		0x14
#define ZSPI_TX_DATA		0x1c
#define ZSPI_RX_DATA		0x20
#define ZSPI_TX_THRESHOLD	0x28
#define ZSPI_RX_THRESHOLD	0x2c
#define ZSPI_FIFO_DEPTH		128
#define ZSPI_NSLAVES		3

#endif
