/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: environment for unchanged pinned BSD coex source slices. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "rtw8822c-coex-model.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define BIT(n) (1U << (n))
#define GENMASK(h,l) ((~0U << (l)) & (~0U >> (31-(h))))
#define __ffs(x) __builtin_ctz(x)
#define EXPORT_SYMBOL(x)
#define rtw_dbg(...) ((void)0)
#define rtw_err(...) abort()
#define WARN(...) abort()
#define udelay(us) model_delay(us)
#include "reg.h"
#include "enums.h"
#define RFREG_MASK 0xfffffU
#define MASKLWORD 0x0000ffffU
enum rtw_rf_path { RF_PATH_A, RF_PATH_B };
struct rtw_coex_rfe {
	u8 rfe_module_type, ant_switch_polarity;
	bool ant_switch_exist, ant_switch_with_bt, ant_switch_diversity, wlg_at_btg;
};
struct rtw_coex_stat { bool bt_disabled; };
struct rtw_coex_dm { u8 cur_ant_pos_type; };
struct rtw_coex {
	struct rtw_coex_rfe rfe;
	struct rtw_coex_stat stat;
	struct rtw_coex_dm dm;
	bool manual_control, stop_dm;
};
struct rtw_efuse { u8 rfe_option; bool share_ant; };
struct rtw_ltecoex_addr { u32 ctrl, wdata, rdata; };
struct rtw_hw_reg { u32 addr, mask; };
struct rtw_chip_info {
	const struct rtw_ltecoex_addr *ltecoex_addr;
	const struct rtw_hw_reg *btg_reg;
	const u32 *rf_base_addr;
};
struct rtw_hal { unsigned rf_phy_num; };
struct rtw_dev {
	const struct rtw_chip_info *chip;
	struct rtw_coex coex;
	struct rtw_efuse efuse;
	struct rtw_hal hal;
};
static u8 rtw_read8(struct rtw_dev *d,u32 a) { (void)d; return model_read(a,1); }
static u16 rtw_read16(struct rtw_dev *d,u32 a) { (void)d; return model_read(a,2); }
static u32 rtw_read32(struct rtw_dev *d,u32 a) { (void)d; return model_read(a,4); }
static void rtw_write8(struct rtw_dev*d,u32 a,u8 v) { (void)d; model_write(a,1,v); }
static void rtw_write16(struct rtw_dev*d,u32 a,u16 v) { (void)d; model_write(a,2,v); }
static void rtw_write32(struct rtw_dev*d,u32 a,u32 v) { (void)d; model_write(a,4,v); }
static void rtw_write8_set(struct rtw_dev*d,u32 a,u8 v) { rtw_write8(d,a,rtw_read8(d,a)|v); }
static void rtw_write8_clr(struct rtw_dev*d,u32 a,u8 v) { rtw_write8(d,a,rtw_read8(d,a)&~v); }
static void rtw_write16_set(struct rtw_dev*d,u32 a,u16 v) { rtw_write16(d,a,rtw_read16(d,a)|v); }
static void rtw_write32_set(struct rtw_dev*d,u32 a,u32 v) { rtw_write32(d,a,rtw_read32(d,a)|v); }
static u32 rtw_read32_mask(struct rtw_dev*d,u32 a,u32 m) { return (rtw_read32(d,a)&m)>>__ffs(m); }
static void rtw_write32_mask(struct rtw_dev*d,u32 a,u32 m,u32 v) {
	rtw_write32(d,a,(rtw_read32(d,a)&~m)|((v<<__ffs(m))&m)); }
static void rtw_write8_mask(struct rtw_dev*d,u32 a,u32 m,u8 v) {
	rtw_write8(d,a,(rtw_read8(d,a)&~m)|((v<<__ffs(m))&m)); }
/* No BT RF calibration is active in this explicitly Wi-Fi-only consumer. */
static void rtw_coex_check_rfk(struct rtw_dev*d) { (void)d; }
/* All RTL8822C RFE profiles mark the external antenna switch absent. */
static void rtw_coex_set_ant_switch(struct rtw_dev*d,u8 c,u8 p) {
	(void)d; (void)c; (void)p; abort(); }
/* Wi-Fi-only init stops DM. These paths must never run on band notification. */
static void rtw_coex_run_coex(struct rtw_dev*d,u8 r) { (void)d; (void)r; abort(); }
static void rtw_coex_scan_notify(struct rtw_dev*d,u8 r) { (void)d; (void)r; abort(); }
#define rtw_write_rf rtw_phy_write_rf_reg
#include "body.c"
int
main(void)
{
	const struct rtw_ltecoex_addr lte = { 0x1700,0x1704,0x1708 };
	const u32 rf_base[] = { 0x3c00,0x4c00 };
	const struct rtw_chip_info chip = { .ltecoex_addr=&lte, .rf_base_addr=rf_base };
	unsigned s,r,i;
	puts("# Unchanged FreeBSD a143197801c680b958ab662c88cf086668a1958b coex slices");
	for(s=0;s<2;s++) for(r=0;r<7;r++) {
		struct rtw_dev d = { .chip=&chip, .hal.rf_phy_num=2,
		    .coex.stat.bt_disabled=true, .efuse.rfe_option=(u8)r };
		model_reset();
		if(s==0) {
			rtw_write8_set(&d,REG_SYS_FUNC_EN,BIT_FEN_BB_GLB_RST|BIT_FEN_BB_RSTB);
			rtw8822c_coex_cfg_rfe_type(&d);
			rtw_coex_set_ant_path(&d,true,COEX_SET_ANT_POWERON);
			rtw_write8(&d,0xff1a,0);
			rtw8822c_coex_cfg_gnt_debug(&d);
		} else {
			rtw_write8_set(&d,REG_BCN_CTRL,BIT_EN_BCN_FUNCTION);
			rtw8822c_coex_cfg_rfe_type(&d);
			rtw8822c_coex_cfg_init(&d);
			rtw_coex_set_wl_pri_mask(&d,COEX_WLPRI_TX_RSP,1);
			rtw_coex_set_wl_pri_mask(&d,COEX_WLPRI_TX_BEACON,1);
			rtw_coex_set_wl_pri_mask(&d,COEX_WLPRI_TX_BEACONQ,1);
			rtw_coex_set_ant_path(&d,true,COEX_SET_ANT_WONLY);
		}
		printf("stage %u rfe %u events %u\n",s,r,ntrace);
		for(i=0;i<ntrace;i++) puts(trace[i]);
		/* The original Wi-Fi-only caller sets stop_dm after WONLY. */
		d.coex.stop_dm=true;
		i=ntrace;
		rtw_coex_switchband_notify(&d,COEX_SWITCH_TO_5G);
		rtw_coex_switchband_notify(&d,COEX_SWITCH_TO_24G_NOFORSCAN);
		if(ntrace!=i) abort();
	}
	return 0;
}
