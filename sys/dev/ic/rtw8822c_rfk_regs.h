/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD d2145e9b96448da34d3cfede24c509af6d830c92 rtw8822c.c.
 * Native private phase bodies; included only by the complete owning consumer.
 * Errors, freshness and cancellation are enforced by its typed IO services.
 * This native adaptation does not establish physical RFK acceptance. */
#ifndef _DEV_IC_RTW8822C_RFK_REGS_H_
#define _DEV_IC_RTW8822C_RFK_REGS_H_
#define REG_PMC_DBG_CTRL1 0xa8
#define BITS_PMC_BT_IQK_STS GENMASK(22, 21)
#define REG_ARFR4 0x049c
#define BIT_WL_RFK BIT(0)
#define BITS_RFC_DIRECT		(BIT(31) | BIT(30))
#define BIT_2G_SWING		0x2d
#define BIT_3WIRE_EN		GENMASK(1, 0)
#define BIT_5G_SWING		0x36
#define BIT_AC_QUEUE		GENMASK(7, 0)
#define BIT_ANTSEG		GENMASK(3, 0)
#define BIT_ANT_PATH		GENMASK(1, 0)
#define BIT_BAND	GENMASK(18, 16)
#define BIT_BBMODE		GENMASK(2, 1)
#define BIT_BB_GAIN		GENMASK(18, 14)
#define BIT_BW_RXBB		GENMASK(11, 10)
#define BIT_BW_TXBB		GENMASK(14, 12)
#define BIT_BYPASS_DPD		BIT(25)
#define BIT_CCA_ON_BY_PW	GENMASK(11, 3)
#define BIT_CFIR_EN		GENMASK(26, 24)
#define BIT_DATA_L	GENMASK(11, 0)
#define BIT_DBG_CCK_CCA		BIT(1)
#define BIT_DE_PWR_TRIM		BIT(19)
#define BIT_DE_TRXBW		BIT(2)
#define BIT_DE_TX_GAIN		BIT(16)
#define BIT_DIS_SHARERX_TXGAT	BIT(27)
#define BIT_DPD_CLK		GENMASK(7, 4)
#define BIT_DPD_EN		BIT(31)
#define BIT_EN_IOQ_IQK_DPK	BIT(30)
#define BIT_GAIN_EXT	BIT(12)
#define BIT_GAIN_RST		BIT(15)
#define BIT_GAIN_TXBB		GENMASK(4, 0)
#define BIT_GAIN_TX_PAD_H	GENMASK(11, 8)
#define BIT_GAIN_TX_PAD_L	GENMASK(7, 4)
#define BIT_GAPK_RPT0		GENMASK(3, 0)
#define BIT_GAPK_RPT1		GENMASK(7, 4)
#define BIT_GAPK_RPT2		GENMASK(11, 8)
#define BIT_GAPK_RPT3		GENMASK(15, 12)
#define BIT_GAPK_RPT4		GENMASK(19, 16)
#define BIT_GAPK_RPT5		GENMASK(23, 20)
#define BIT_GAPK_RPT6		GENMASK(27, 24)
#define BIT_GAPK_RPT7		GENMASK(31, 28)
#define BIT_GAPK_RPT_IDX	GENMASK(11, 8)
#define BIT_GLOSS_DB		GENMASK(14, 12)
#define BIT_GS_PWSF		GENMASK(27, 0)
#define BIT_INNER_LB		BIT(21)
#define BIT_IQK_DPK_CLOCK_SRC	BIT(28)
#define BIT_IQK_DPK_EN		BIT(17)
#define BIT_IQK_DPK_RESET_SRC	BIT(29)
#define BIT_IQ_SWITCH		GENMASK(5, 0)
#define BIT_IRQ_TEST_MODE	BIT(20)
#define BIT_I_GAIN		GENMASK(19, 16)
#define BIT_LB_ATT		GENMASK(4, 2)
#define BIT_LB_SW		GENMASK(13, 12)
#define BIT_PATH_EN		BIT(31)
#define BIT_PS_EN		BIT(7)
#define BIT_PW_EXT_TIA		BIT(1)
#define BIT_Q_GAIN		GENMASK(11, 0)
#define BIT_Q_GAIN_SEL		GENMASK(14, 12)
#define BIT_RFTXEN_GCK_FORCE_ON	BIT(31)
#define BIT_RF_GAIN		GENMASK(4, 2)
#define BIT_RF_MODE		GENMASK(19, 16)
#define BIT_RPT_CIP_STATUS	GENMASK(7, 0)
#define BIT_RPT_DGAIN		GENMASK(27, 16)
#define BIT_RPT_EN		BIT(21)
#define BIT_RPT_SEL		GENMASK(20, 16)
#define BIT_RXAGC		GENMASK(9, 5)
#define BIT_RXA_MIX_GAIN	GENMASK(4, 3)
#define BIT_RXG_GAIN		BIT(18)
#define BIT_SEL_PATH		GENMASK(2, 1)
#define BIT_STOP_TX		GENMASK(3, 0)
#define BIT_SUBPAGE		GENMASK(3, 0)
#define BIT_TIA_BYPASS		BIT(5)
#define BIT_TST_IQK2SET_SRC	BIT(31)
#define BIT_TXAGC		GENMASK(4, 0)
#define BIT_TXA_LB_ATT		GENMASK(15, 14)
#define BIT_TXA_TANK	BIT(4)
#define BIT_TXBB		BIT(10)
#define BIT_TX_CCK_IND		BIT(16)
#define BIT_TX_CFIR		GENMASK(31, 30)
#define BIT_TX_MODE		GENMASK(19, 8)
#define BIT_TX_SCALE_0DB	BIT(7)
#define DPK_CHANNEL_WIDTH_80 1
#define DPK_RF_PATH_NUM 2
#define DPK_RF_REG_NUM 7
#define DPK_RF_REG_NUM 7
#define REG_3WIRE		0x180c
#define REG_3WIRE2		0x410c
#define REG_ANAPAR_A		0x1830
#define REG_ANAPAR_B		0x4130
#define REG_ANTMAP0		0x820
#define REG_CCA_OFF		0x1d58
#define REG_CCKSB		0x1a00
#define REG_CH_DELAY_EXTR2	0x1cd0
#define REG_DIS_SHARE_RX_A	0x186c
#define REG_DIS_SHARE_RX_B	0x416c
#define REG_DPD_AGC		0x1b67
#define REG_DPD_CTL0		0x1bb4
#define REG_DPD_CTL0_S0		0x1b04
#define REG_DPD_CTL0_S1		0x1b5c
#define REG_DPD_CTL11		0x1be4
#define REG_DPD_CTL12		0x1be8
#define REG_DPD_CTL15		0x1bf4
#define REG_DPD_CTL16		0x1bf8
#define REG_DPD_CTL1_S0		0x1b08
#define REG_DPD_CTL1_S1		0x1b60
#define REG_DPD_LUT0		0x1b44
#define REG_ENFN		0x1e24
#define REG_IQKSTAT		0x1b10
#define REG_IQK_CTL1		0x1b20
#define REG_IQK_CTRL		0x1c38
#define REG_NCTL0		0x1b00
#define REG_ORITXCODE		0x1800
#define REG_ORITXCODE2		0x4100
#define REG_RFTXEN_GCK_A	0x1864
#define REG_RFTXEN_GCK_B	0x4164
#define REG_RPT_CIP		0x2d9c
#define REG_RSV_CTRL		0x001C
#define REG_RXSRAM_CTL		0x1bd4
#define REG_R_CONFIG		0x1bcc
#define REG_SINGLE_TONE_SW	0x1bb8
#define REG_STAT_RPT		0x1bfc
#define REG_TABLE_SEL		0x1b98
#define REG_TXANTSEG		0x1e28
#define REG_TXLGMAP		0x1e2c
#define REG_TXPAUSE		0x0522
#define REG_TX_FIFO		0x1e70
#define REG_TX_GAIN_SET		0x1b9c
#define REG_TX_PTCL_CTRL	0x0520
#define REG_TX_TONE_IDX		0x1b2c
#define REG_WLRF1		0x00EC
#define RFREG_MASK			0xfffff
#define RF_BW_TRXBB		0x1a
#define RF_CFGCH	0x18
#define RF_DEBUG		0xde
#define RF_DIS_BYPASS_TXBB	0x9e
#define RF_EXT_TIA_BW		0x8f
#define RF_GAIN_NUM 11
#define RF_HW_OFFSET_NUM 10
#define RF_IDAC			0x58
#define RF_LUTDBG	0xdf
#define RF_LUTWA	0x33
#define RF_LUTWD0	0x3f
#define RF_LUTWE2	0xee
#define RF_MODE_TRXAGC		0x00
#define RF_RXAGC_OFFSET		0x19
#define RF_RXA_MIX_GAIN		0x8a
#define RF_RXG_GAIN		0x87
#define RF_TXA_LB_SW		0x63
#define RF_TX_GAIN		0x56
#define RF_TX_GAIN_OFFSET	0x55
#define RF_TX_RESULT		0x5f
#define RF_T_METER	0x42
#endif
