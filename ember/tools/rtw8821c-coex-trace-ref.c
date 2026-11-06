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
#define u8_get_bits(value, mask) (((value) & (mask)) >> __ffs(mask))
#define FIELD_PREP(mask, value) (((value) << __ffs(mask)) & (mask))
#define EXPORT_SYMBOL(x)
#define rtw_dbg(...) ((void)0)
#define rtw_err(...) abort()
#define WARN(...) abort()
#define udelay(us) model_delay(us)
#define mdelay(ms) model_delay((ms) * 1000)
#define TDMA_4SLOT BIT(8)
#define TDMA_TIMER_TYPE_4SLOT 1
#define TDMA_TIMER_TYPE_2SLOT 0
#define RTW_FLAG_BUSY_TRAFFIC 0
#define test_bit(bit, flags) (0)
#define COEX_MIN_DELAY 10
#define COEX_RFK_TIMEOUT 600
#define COEX_CNT_WL_MAX 64
#define COEX_CNT_WL_5MS_NOEXTEND 0
#define COEX_CNT_BT_MAX 64
#define DESC_RATE5_5M 2
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define HZ 100
#define ieee80211_queue_delayed_work(...) abort()
#define COEX_H2C69_WL_LEAKAP 0x0c
#define PARA1_H2C69_DIS_5MS 0x01
#define PARA1_H2C69_EN_5MS 0x00
#include "reg.h"
#include "enums.h"
#define RFREG_MASK 0xfffffU
#define MASKLWORD 0x0000ffffU
enum rtw_rf_path { RF_PATH_A, RF_PATH_B };
struct rtw_coex_rfe {
	u8 rfe_module_type, ant_switch_polarity;
	bool ant_switch_exist, ant_switch_with_bt, ant_switch_diversity, wlg_at_btg;
};
struct rtw_coex_stat { bool bt_disabled, wl_slot_extend, wl_slot_toggle_change, wl_slot_toggle, bt_a2dp_exist, bt_inq_remain, bt_multi_link, bt_reenable, bt_mailbox_reply; u8 kt_ver, bt_iqk_state; u16 score_board; unsigned cnt_wl[COEX_CNT_WL_MAX], cnt_bt[COEX_CNT_BT_MAX]; unsigned hi_pri_tx,hi_pri_rx,lo_pri_tx,lo_pri_rx,bt_disable_cnt,bt_ble_scan_type; u8 wl_coex_mode,wl_rx_rate,wl_rts_rx_rate; };
struct rtw_coex_dm { u8 cur_ant_pos_type, cur_table, reason, cur_ps_tdma, cur_bt_lna_lvl, ps_tdma_para[5],bt_rssi_state[4],wl_rssi_state[4]; bool cur_ps_tdma_on; u32 cur_switch_status; };
struct rtw_coex {
	struct rtw_coex_rfe rfe;
	struct rtw_coex_stat stat;
	struct rtw_coex_dm dm;
	bool manual_control, stop_dm, wl_rf_off, freeze; int bt_reenable_work;
};
struct rtw_efuse { u8 rfe_option; bool share_ant; };
struct rtw_ltecoex_addr { u32 ctrl, wdata, rdata; };
struct rtw_hw_reg { u32 addr, mask; };
struct coex_table_para { u32 bt, wl; };
struct coex_tdma_para { u8 para[5]; };
struct rtw_chip_info {
	const struct rtw_ltecoex_addr *ltecoex_addr;
	const struct rtw_hw_reg *btg_reg;
	const u32 *rf_base_addr;
	bool scbd_support, new_scbd10_def; int pstdma_type;
	unsigned table_sant_num, table_nsant_num;
	const struct coex_table_para *table_sant, *table_nsant;
	unsigned tdma_sant_num, tdma_nsant_num;
	const struct coex_tdma_para *tdma_sant, *tdma_nsant;
};
struct rtw_hal { unsigned rf_phy_num; };
struct rtw_dev {
	const struct rtw_chip_info *chip;
	struct rtw_coex coex;
	struct rtw_efuse efuse;
	struct rtw_hal hal; unsigned flags; void *hw;
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
static void rtw_write32_clr(struct rtw_dev*d,u32 a,u32 v) { rtw_write32(d,a,rtw_read32(d,a)&~v); }
static u32 rtw_read32_mask(struct rtw_dev*d,u32 a,u32 m) { return (rtw_read32(d,a)&m)>>__ffs(m); }
static void rtw_write32_mask(struct rtw_dev*d,u32 a,u32 m,u32 v) {
	rtw_write32(d,a,(rtw_read32(d,a)&~m)|((v<<__ffs(m))&m)); }
static void rtw_write8_mask(struct rtw_dev*d,u32 a,u32 m,u8 v) {
	rtw_write8(d,a,(rtw_read8(d,a)&~m)|((v<<__ffs(m))&m)); }
#define rtw_coex_set_ant_switch rtw8821c_coex_cfg_ant_switch
/* Wi-Fi-only init stops DM. These paths must never run on band notification. */
static void rtw_coex_run_coex(struct rtw_dev*d,u8 r) { (void)d; (void)r; abort(); }
static void rtw_coex_scan_notify(struct rtw_dev*d,u8 r) { (void)d; (void)r; abort(); }
/* Scoped models of donor leaf services; full donor top-level functions below. */
static void rtw8821c_coex_cfg_rfe_type(struct rtw_dev *);
static void rtw8821c_coex_cfg_init(struct rtw_dev *);
static void rtw_coex_set_ant_path(struct rtw_dev *, bool, u8);
static void rtw_coex_set_wl_pri_mask(struct rtw_dev *, u8, u8);
static void rtw8821c_coex_cfg_gnt_debug(struct rtw_dev *);
#define rtw_coex_set_rfe_type rtw8821c_coex_cfg_rfe_type
#define rtw_coex_set_init rtw8821c_coex_cfg_init
#define rtw_coex_set_gnt_debug rtw8821c_coex_cfg_gnt_debug
static void rtw_coex_init_coex_var(struct rtw_dev *);
static void rtw_coex_monitor_bt_enable(struct rtw_dev *);
static void rtw_fw_bt_wifi_control(struct rtw_dev *d,u8 op,u8 *data) {
 (void)d; record('h',0,0x69,op | ((u32)data[0] << 8));
}
static void rtw_btc_wltoggle_table_a(struct rtw_dev*d,bool force,u8 type) {
 (void)d; (void)force; (void)type; abort();
}
static void rtw_coex_write_scbd(struct rtw_dev *, u16, bool);
static void rtw_coex_table(struct rtw_dev *, bool, u8);
/* This fixture fixes initial TDMA=2-slot and an idle, awake WLAN. */
static void rtw_coex_tdma_timer_base(struct rtw_dev *d,u8 type) {
 (void)d; if(type != 0) abort();
}
static void rtw_coex_power_save_state(struct rtw_dev *d,u8 type,u8 a,u8 b) {
 (void)d; if(type != COEX_PS_WIFI_NATIVE || a || b) abort();
}
static void rtw_fw_coex_tdma_type(struct rtw_dev *d,u8 a,u8 b,u8 c,u8 e,u8 f) {
 (void)d; if(a||b||c||e||f) abort(); record('h',0,0x60,0);
}
static void rtw_coex_tdma(struct rtw_dev *, bool, u32);
static void rtw_fw_query_bt_info(struct rtw_dev *d) {
 (void)d; record('h',0,0x61,1);
}
static void rtw_coex_query_bt_info(struct rtw_dev *);
#define rtw_write_rf rtw_phy_write_rf_reg
#include "body.c"
int
main(void)
{
	const struct rtw_ltecoex_addr lte = { 0x1700,0x1704,0x1708 };
	const u32 rf_base[] = { 0x3c00,0x4c00 };
	const struct coex_table_para shared[] = { {0x55555555,0x55555555}, {0x55555555,0x55555555} };
	const struct coex_tdma_para tdma[] = { {{0,0,0,0,0}} };
	const struct rtw_chip_info chip = { .ltecoex_addr=&lte, .rf_base_addr=rf_base, .scbd_support=true, .table_sant=shared, .table_sant_num=2, .tdma_sant=tdma, .tdma_sant_num=1 };
	static const unsigned profiles[] = {0, 2, 4, 6};
	unsigned r,i,j,share;
	puts("# FreeBSD a143197 RTL8821C complete WONLY top-level; BT disabled");
	for(j=0;j<sizeof(profiles)/sizeof(profiles[0]);j++) {
	 for(share=0;share<2;share++) {
		r=profiles[j];
		struct rtw_dev d = { .chip=&chip, .hal.rf_phy_num=1,
		    .coex.stat.bt_disabled=true, .efuse.rfe_option=(u8)r,
		    .efuse.share_ant=share };
		model_reset();
		/* Explicit Wi-Fi-only scenario: donor monitor must see BT off. */
		poke(REG_WIFI_BT_INFO,2,0);
		poke(REG_ARFR4,1,0);
		rtw_coex_power_on_setting(&d);
		__rtw_coex_init_hw_config(&d,true);
		printf("rfe %u share %u events %u\n",r,share,ntrace);
		for(i=0;i<ntrace;i++) puts(trace[i]);
	 }
	}
	return 0;
}
