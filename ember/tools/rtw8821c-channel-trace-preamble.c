/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Trace harness; register definitions from FreeBSD a143197801c680b958ab662c88cf086668a1958b. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t u32; typedef uint8_t u8;
#define BIT(n) (1U<<(n))
#define GENMASK(h,l) ((0xffffffffU<<(l))&(0xffffffffU>>(31-(h))))
#define MASKDWORD 0xffffffffU
#define MASKLWORD 0xffffU
#define MASKBYTE0 0xffU
#define MASKBYTE2 0xff0000U
#include "reg.h"
#define REG_SYS_CTRL	0x000
#define BIT_FEN_EN	BIT(26)
#define REG_INIRTS_RATE_SEL 0x0480
#define REG_HTSTFWT	0x800
#define REG_RXCCAMSK	0x814
#define REG_L1WT	0x83c
#define REG_L1PKWT	0x840
#define REG_MRC		0x850
#define REG_ADC40	0x8c8
#define REG_CHFIR	0x8f0
#define REG_CDDTXP	0x93c
#define REG_TXPSEL1	0x940
#define REG_ACBB0	0x948
#define REG_ACBBRXFIR	0x94c
#define REG_ACGG2TBL	0x958
#define REG_ADCINI	0xa04
#define REG_PWRTH	0xa08
#define REG_CCA_FLTR	0xa20
#define REG_TXSF2	0xa24
#define REG_TXSF6	0xa28
#define REG_RXDESC	0xa2c
#define REG_ENTXCCK	0xa80
#define BTG_LNA		0xfc84
#define WLG_LNA		0x7532
#define REG_ENRXCCA	0xa84
#define BTG_CCA		0x0e
#define WLG_CCA		0x12
#define REG_PWRTH2	0xaa8
#define REG_CSRATIO	0xaaa
#define REG_TXFILTER	0xaac
#define REG_AGCTR_A	0xc08
#define REG_TXDFIR	0xc20
#define REG_TRSW	0xca0
#define REG_RFESEL0	0xcb0
#define REG_RFESEL8	0xcb4
#define REG_RFECTL	0xcb8
#define B_BTG_SWITCH	BIT(16)
#define B_CTRL_SWITCH	BIT(18)
#define B_WL_SWITCH	(BIT(20) | BIT(22))
#define B_WLG_SWITCH	BIT(21)
#define B_WLA_SWITCH	BIT(23)
#define REG_RFEINV	0xcbc
#define REG_AGCTR_B	0xe08
#define REG_DMEM_CTRL	0x1080
#define BIT_WL_RST	BIT(16)
#define REG_ANTWT	0x1904
#define REG_IQKFAILMSK	0x1bf0
#define BIT_MASK_R_RFE_SEL_15	GENMASK(31, 28)
#define BIT_SDIO_INT BIT(18)
#define BT_CNT_ENABLE	0x1
#define BIT_BCN_QUEUE	BIT(3)
#define BCN_PRI_EN	0x1
#define PTA_CTRL_PIN	0x66
#define DPDT_CTRL_PIN	0x77
#define ANTDIC_CTRL_PIN	0x88
#define REG_CTRL_TYPE	0x67
#define BIT_CTRL_TYPE1	BIT(5)
#define BIT_CTRL_TYPE2	BIT(4)
#define CTRL_TYPE_MASK	GENMASK(15, 8)
#define RF18_BAND_2G 0
#define RF18_BAND_5G (BIT(16)|BIT(8))
#define RF18_RFSI_GE BIT(17)
#define RF18_RFSI_GT BIT(18)
#define RF18_BW_MASK (BIT(11)|BIT(10))
#define RF18_BW_20M (BIT(11)|BIT(10))
#define RF18_BW_40M BIT(11)
#define RF18_BW_80M BIT(10)
#define RFREG_MASK 0xfffff
#define RF_PATH_A 0
#define SWITCH_TO_BTG 1
#define SWITCH_TO_WLG 2
#define SWITCH_TO_WLA 3
#define SWITCH_TO_BT 4
#define RTW_CHANNEL_WIDTH_20 0
#define RTW_CHANNEL_WIDTH_40 1
#define RTW_CHANNEL_WIDTH_80 2
#define RTW_CHANNEL_WIDTH_5 5
#define RTW_CHANNEL_WIDTH_10 6
#define RTW_SC_20_UPPER 1
#define RTW_SC_20_UPMOST 3
#define RTW_SC_40_UPPER 9
#define RTW_SC_40_LOWER 10
#define IS_CH_5G_BAND(x) ((x)>14)
struct rtw_hal { int rfe_btg;u32 ch_param[3]; };
struct rtw_efuse {u8 tx_bb_swing_setting_2g,tx_bb_swing_setting_5g;};
struct rtw_dev {struct rtw_hal hal;struct rtw_efuse efuse;};
static uint8_t regs[0x4000];static uint64_t hash=14695981039346656037ULL;static unsigned events;
static void ev(uint64_t a,uint64_t b,uint64_t c){uint64_t v[3]={a,b,c};for(unsigned i=0;i<3;i++)for(unsigned j=0;j<8;j++){hash^=(v[i]>>(j*8))&255;hash*=1099511628211ULL;}events++;}
static u8 rtw_read8(struct rtw_dev*d,u32 a){(void)d;return regs[a];}
static u32 rtw_read32(struct rtw_dev*d,u32 a){u32 v;(void)d;memcpy(&v,regs+a,4);return v;}
static void rtw_write8(struct rtw_dev*d,u32 a,u8 v){(void)d;regs[a]=v;ev(1,a,v);}
static void rtw_write32(struct rtw_dev*d,u32 a,u32 v){(void)d;memcpy(regs+a,&v,4);ev(4,a,v);}
static void rtw_write16(struct rtw_dev*d,u32 a,uint16_t v){(void)d;memcpy(regs+a,&v,2);ev(2,a,v);}
static u32 rtw_read32_mask(struct rtw_dev*d,u32 a,u32 m){return (rtw_read32(d,a)&m)>>__builtin_ctz(m);}
static void rtw_write32_mask(struct rtw_dev*d,u32 a,u32 m,u32 v){u32 old=rtw_read32(d,a);rtw_write32(d,a,(old&~m)|((v<<__builtin_ctz(m))&m));}
static void rtw_write32_set(struct rtw_dev*d,u32 a,u32 b){rtw_write32(d,a,rtw_read32(d,a)|b);}
static void rtw_write32_clr(struct rtw_dev*d,u32 a,u32 b){rtw_write32(d,a,rtw_read32(d,a)&~b);}
static u32 rtw_read_rf(struct rtw_dev*d,int p,u32 a,u32 m){(void)p;return rtw_read32_mask(d,0x2800+(a<<2),m);}
static void rtw_write_rf(struct rtw_dev*d,int p,u32 a,u32 m,u32 v){u32 x;(void)p;if(m!=RFREG_MASK){x=rtw_read_rf(d,0,a,RFREG_MASK);v=(x&~m)|(v<<__builtin_ctz(m));}rtw_write32(d,0xc90,(a<<20)|(v&0xfffff));v&=0xfffff;memcpy(regs+0x2800+(a<<2),&v,4);ev(5,13,0);}
static int rtw_chip_wcpu_8051(struct rtw_dev*d){(void)d;return 0;}
static int rtw_regd_srrc(struct rtw_dev*d){(void)d;return 0;}
static void rtw8821c_cck_tx_filter_srrc(struct rtw_dev*d,u8 c,u8 b){(void)d;(void)c;(void)b;}
static void rtw8821c_pwrtrack_init(struct rtw_dev*d){(void)d;}
