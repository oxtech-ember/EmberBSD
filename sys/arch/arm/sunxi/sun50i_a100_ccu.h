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

#ifndef _SUN50I_A100_CCU_H
#define _SUN50I_A100_CCU_H

/*
 * Clock and reset numbers of the Allwinner A100 and A133, as the device
 * tree bindings give them.
 */

#define	A100_RST_MBUS		0
#define	A100_RST_BUS_GPU	3
#define	A100_RST_BUS_DMA	6
#define	A100_RST_BUS_HSTIMER	9
#define	A100_RST_BUS_PWM	12
#define	A100_RST_BUS_MMC0	15
#define	A100_RST_BUS_MMC1	16
#define	A100_RST_BUS_MMC2	17
#define	A100_RST_BUS_UART0	18
#define	A100_RST_BUS_UART1	19
#define	A100_RST_BUS_UART2	20
#define	A100_RST_BUS_UART3	21
#define	A100_RST_BUS_UART4	22
#define	A100_RST_BUS_I2C0	23
#define	A100_RST_BUS_I2C1	24
#define	A100_RST_BUS_I2C2	25
#define	A100_RST_BUS_I2C3	26
#define	A100_RST_BUS_SPI0	27
#define	A100_RST_BUS_SPI1	28
#define	A100_RST_BUS_SPI2	29
#define	A100_RST_BUS_EMAC	30
#define	A100_RST_BUS_THS	34
#define	A100_RST_USB_PHY0	42
#define	A100_RST_USB_PHY1	43
#define	A100_RST_BUS_OHCI0	44
#define	A100_RST_BUS_OHCI1	45
#define	A100_RST_BUS_EHCI0	46
#define	A100_RST_BUS_EHCI1	47
#define	A100_RST_BUS_OTG	48

#define	A100_CLK_OSC12M		0
#define	A100_CLK_PLL_GPU	7	/* internal CCU clock, not DT binding */
#define	A100_CLK_PLL_PERIPH0	3
#define	A100_CLK_PLL_PERIPH0_2X	4
#define	A100_CLK_PLL_PERIPH1	5
#define	A100_CLK_PLL_PERIPH1_2X	6
#define	A100_CLK_PSI_AHB1_AHB2	27
#define	A100_CLK_AHB3		28
#define	A100_CLK_APB1		29
#define	A100_CLK_APB2		30
#define	A100_CLK_GPU		36
#define	A100_CLK_BUS_GPU	37
#define	A100_CLK_BUS_DMA	42
#define	A100_CLK_BUS_HSTIMER	45
#define	A100_CLK_BUS_PWM	49
#define	A100_CLK_MMC0		62
#define	A100_CLK_MMC1		63
#define	A100_CLK_MMC2		64
#define	A100_CLK_BUS_MMC0	66
#define	A100_CLK_BUS_MMC1	67
#define	A100_CLK_BUS_MMC2	68
#define	A100_CLK_BUS_UART0	69
#define	A100_CLK_BUS_UART1	70
#define	A100_CLK_BUS_UART2	71
#define	A100_CLK_BUS_UART3	72
#define	A100_CLK_BUS_UART4	73
#define	A100_CLK_BUS_I2C0	74
#define	A100_CLK_BUS_I2C1	75
#define	A100_CLK_BUS_I2C2	76
#define	A100_CLK_BUS_I2C3	77
#define	A100_CLK_BUS_SPI0	81
#define	A100_CLK_BUS_SPI1	82
#define	A100_CLK_BUS_SPI2	83
#define	A100_CLK_EMAC_25M	84
#define	A100_CLK_BUS_EMAC	85
#define	A100_CLK_BUS_THS	91
#define	A100_CLK_USB_OHCI0	108
#define	A100_CLK_USB_PHY0	109
#define	A100_CLK_USB_OHCI1	110
#define	A100_CLK_USB_PHY1	111
#define	A100_CLK_BUS_OHCI0	112
#define	A100_CLK_BUS_OHCI1	113
#define	A100_CLK_BUS_EHCI0	114
#define	A100_CLK_BUS_EHCI1	115
#define	A100_CLK_BUS_OTG	116

#endif /* !_SUN50I_A100_CCU_H */
