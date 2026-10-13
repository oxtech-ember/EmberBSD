/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _SUN60I_A733_R_CCU_H
#define _SUN60I_A733_R_CCU_H

/* The numbers of the binding allwinner,sun60i-a733-r-ccu. */
#define	A733_R_CLK_AHB			0
#define	A733_R_CLK_APB0			1
#define	A733_R_CLK_APB1			2
#define	A733_R_CLK_BUS_UART0		14
#define	A733_R_CLK_BUS_UART1		15
#define	A733_R_CLK_BUS_I2C0		16
#define	A733_R_CLK_BUS_I2C1		17
#define	A733_R_CLK_BUS_I2C2		18
#define	A733_R_CLK_BUS_PPU		19
#define	A733_R_CLK_BUS_RTC		24

#define	A733_R_RST_BUS_UART0		4
#define	A733_R_RST_BUS_UART1		5
#define	A733_R_RST_BUS_I2C0		6
#define	A733_R_RST_BUS_I2C1		7
#define	A733_R_RST_BUS_I2C2		8
#define	A733_R_RST_BUS_RTC		10

#endif /* _SUN60I_A733_R_CCU_H */
