/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C PHY reference harness, AI-assisted. */
/*
 * Minimal host environment for the unchanged reference PHY and channel
 * functions. Built only by rtw8822c-phy-trace-gen.sh; the extracted
 * reference body is included at REF_BODY and is not redistributed here.
 * The table load and rf_init are markers: the tables have their own
 * contract, and rf_init (DAC calibration, trims, PA bias) is TX work.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtw8822c-mac-model.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define __ffs(x) ((u32)__builtin_ctz(x))
#define rtw_err(dev, ...) fprintf(stderr, __VA_ARGS__)
#define rtw_dbg(dev, ...) do { } while (0)
#define rtw_warn(dev, ...) fprintf(stderr, __VA_ARGS__)
#define udelay(x) do { } while (0)
#define mdelay(x) do { } while (0)

#include "reg.h"
#include "defs-rtw8822c.h"
#include "defs-bf.h"

#include "defs-phy.h"
#include "defs-main.h"
#include "defs-mac.h"
#define EFUSE_READ_FAIL 0xff	/* efuse.h */
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define FIELD_GET(m, v) (((v) & (m)) >> __ffs(m))
#define FIELD_PREP(m, v) (((v) << __ffs(m)) & (m))
#define CCK_PD_LV0 0		/* phy.h enum rtw_cck_pd_lv, first member */
#define DESC_RATE6M 0x04	/* main.h enum rtw_desc_rate */

enum rtw_rf_path { RF_PATH_A, RF_PATH_B, RF_PATH_C, RF_PATH_D };
#define RTW_RF_PATH_MAX 4
enum rtw_bb_path {
	BB_PATH_A = 1, BB_PATH_B = 2, BB_PATH_AB = 3
};
enum rtw_bandwidth {
	RTW_CHANNEL_WIDTH_20 = 0, RTW_CHANNEL_WIDTH_40 = 1,
	RTW_CHANNEL_WIDTH_80 = 2, RTW_CHANNEL_WIDTH_160 = 3,
	RTW_CHANNEL_WIDTH_80_80 = 4, RTW_CHANNEL_WIDTH_5 = 5,
	RTW_CHANNEL_WIDTH_10 = 6
};
enum { RTW_SC_DONT_CARE = 0, RTW_SC_20_UPPER = 1, RTW_SC_20_LOWER = 2,
	RTW_SC_20_UPMOST = 3, RTW_SC_20_LOWEST = 4, RTW_SC_40_UPPER = 9,
	RTW_SC_40_LOWER = 10 };
enum { RTW_WCPU_3081, RTW_WCPU_8051 };
enum rtw_edcca_mode { RTW_EDCCA_NORMAL = 0, RTW_EDCCA_ADAPTIVITY = 1 };
enum { NL80211_DFS_UNSET, NL80211_DFS_FCC, NL80211_DFS_ETSI, NL80211_DFS_JP };

struct rtw_hw_reg { u32 addr; u32 mask; };
struct rtw_hw_reg_offset { struct rtw_hw_reg hw_reg; u8 offset; };
struct rtw_dev;
struct rtw_chip_ops {
	u32 (*read_rf)(struct rtw_dev *, enum rtw_rf_path, u32, u32);
	void (*adaptivity_init)(struct rtw_dev *);
	void (*cfo_init)(struct rtw_dev *);
};
struct rtw_chip_info {
	int wlan_cpu;
	u32 rf_base_addr[2];
	u32 rf_sipi_addr[2];
	const struct rtw_hw_reg *dig;
	const struct rtw_hw_reg_offset *edcca_th;
	u8 l2h_th_ini_cs, l2h_th_ini_ad;
	u8 default_1ss_tx_path;
	const struct rtw_chip_ops *ops;
};
struct rtw_dm_info {
	u32 fa_history[4];
	u8 igi_history[4];
	u32 igi_bitmap;
	u8 cck_pd_lv[3][RTW_RF_PATH_MAX];
	u32 cck_fa_avg;
	struct { bool done; } iqk;
	enum rtw_edcca_mode edcca_mode;
	u8 l2h_th_ini;
	struct rtw_cfo_track { u8 crystal_cap; bool is_adjust; } cfo_track;
	u8 cck_gi_u_bnd, cck_gi_l_bnd;
};
struct rtw_dev {
	const struct rtw_chip_info *chip;
	struct rtw_dm_info dm_info;
	struct rtw_hal {
		u8 cut_version, rf_phy_num, rf_path_num;
		u8 antenna_tx, antenna_rx;
	} hal;
	struct { u8 crystal_cap, thermal_meter_k; } efuse;
	struct { int dfs_region; } regd;
	struct rtw_path_div {
		u8 current_tx_path;
		u32 path_a_cnt, path_a_sum, path_b_cnt, path_b_sum;
	} dm_path_div;
};

static bool rtw_edcca_enabled = true;
static unsigned mark_tables, mark_rf_init;

static u8 rtw_read8(struct rtw_dev *d, u32 a) { (void)d; return (u8)model_read(a, 1); }
static u16 rtw_read16(struct rtw_dev *d, u32 a) { (void)d; return (u16)model_read(a, 2); }
static u32 rtw_read32(struct rtw_dev *d, u32 a) { (void)d; return model_read(a, 4); }
static void rtw_write8(struct rtw_dev *d, u32 a, u8 v) { (void)d; model_write(a, 1, v); }
static void rtw_write16(struct rtw_dev *d, u32 a, u16 v) { (void)d; model_write(a, 2, v); }
static void rtw_write32(struct rtw_dev *d, u32 a, u32 v) { (void)d; model_write(a, 4, v); }
static void rtw_write8_set(struct rtw_dev *d, u32 a, u8 b) { rtw_write8(d, a, rtw_read8(d, a) | b); }
static void rtw_write8_clr(struct rtw_dev *d, u32 a, u8 b) { rtw_write8(d, a, rtw_read8(d, a) & ~b); }
static void rtw_write16_set(struct rtw_dev *d, u32 a, u16 b) { rtw_write16(d, a, rtw_read16(d, a) | b); }
static void rtw_write16_clr(struct rtw_dev *d, u32 a, u16 b) { rtw_write16(d, a, rtw_read16(d, a) & ~b); }
static void rtw_write32_set(struct rtw_dev *d, u32 a, u32 b) { rtw_write32(d, a, rtw_read32(d, a) | b); }
static void rtw_write32_clr(struct rtw_dev *d, u32 a, u32 b) { rtw_write32(d, a, rtw_read32(d, a) & ~b); }
static u32
rtw_read32_mask(struct rtw_dev *d, u32 a, u32 mask)
{
	return (rtw_read32(d, a) & mask) >> __ffs(mask);
}
static void
rtw_write32_mask(struct rtw_dev *d, u32 a, u32 mask, u32 data)
{
	u32 orig = rtw_read32(d, a);

	rtw_write32(d, a, (orig & ~mask) | ((data << __ffs(mask)) & mask));
}
static bool rtw_chip_wcpu_8051(struct rtw_dev *d) { return d->chip->wlan_cpu == RTW_WCPU_8051; }

u32 rtw_phy_read_rf(struct rtw_dev *, enum rtw_rf_path, u32, u32);
bool rtw_phy_write_rf_reg_mix(struct rtw_dev *, enum rtw_rf_path, u32, u32, u32);
static u32
rtw_read_rf(struct rtw_dev *d, enum rtw_rf_path p, u32 a, u32 m)
{
	return rtw_phy_read_rf(d, p, a, m);
}
static void
rtw_write_rf(struct rtw_dev *d, enum rtw_rf_path p, u32 a, u32 m, u32 v)
{
	(void)rtw_phy_write_rf_reg_mix(d, p, a, m, v);
}

/* Markers for the parts this trace leaves to other contracts. */
static void
rtw_phy_load_tables(struct rtw_dev *d)
{
	(void)d;
	mark_tables = model_ntrace;
}
/* DAC calibration is a measurement loop; it is ported separately. */
static void
rtw8822c_rf_dac_cal(struct rtw_dev *d)
{
	(void)d;
	mark_rf_init = model_ntrace;
}

/*
 * Physical EFUSE image. The native driver reads the whole map once at
 * identify, so this stub performs no register access.
 */
static u8 efuse_phys[512];
static void
rtw_read8_physical_efuse(struct rtw_dev *d, u16 addr, u8 *data)
{
	(void)d;
	*data = efuse_phys[addr & 511];
}
static void rtw8822c_pwrtrack_init(struct rtw_dev *d) { (void)d; }
static void rtw8822c_adaptivity_init(struct rtw_dev *);
static void rtw8822c_cfo_init(struct rtw_dev *);

#include REF_BODY

static const struct rtw_hw_reg dig_8822c[] = {
	{ .addr = 0x1d70, .mask = 0x7f }, { .addr = 0x1d70, .mask = 0x7f00 },
};
static const struct rtw_hw_reg_offset edcca_8822c[] = {
	{ { .addr = 0x84c, .mask = MASKBYTE2 }, .offset = 0x80 },
	{ { .addr = 0x84c, .mask = MASKBYTE3 }, .offset = 0x80 },
};
static const struct rtw_chip_ops ops_8822c = {
	.read_rf = rtw_phy_read_rf,
	.adaptivity_init = rtw8822c_adaptivity_init,
	.cfo_init = rtw8822c_cfo_init,
};
static const struct rtw_chip_info chip_8822c = {
	.wlan_cpu = RTW_WCPU_3081,
	.rf_base_addr = { 0x3c00, 0x4c00 },
	.rf_sipi_addr = { 0x1808, 0x4108 },
	.dig = dig_8822c, .edcca_th = edcca_8822c,
	.l2h_th_ini_cs = 60, .l2h_th_ini_ad = 45,
	.default_1ss_tx_path = BB_PATH_A, .ops = &ops_8822c,
};

static void
setup(struct rtw_dev *dev)
{
	memset(dev, 0, sizeof(*dev));
	dev->chip = &chip_8822c;
	dev->hal.rf_phy_num = 2;
	dev->hal.rf_path_num = 2;
	dev->hal.antenna_tx = BB_PATH_AB;
	dev->hal.antenna_rx = BB_PATH_AB;
	dev->efuse.crystal_cap = 0x3c;		/* Fixed contract input. */
	dev->regd.dfs_region = NL80211_DFS_UNSET;
	model_reset();
}

static void
section(const char *name, unsigned from, unsigned to)
{
	unsigned i;

	printf("section %s writes %u\n", name, to - from);
	for (i = from; i < to; i++)
		printf("%s\n", model_trace[i]);
}

int
main(void)
{
	static const u8 channels[] = { 1, 6, 11, 13, 14, 36, 100, 149 };
	struct rtw_dev dev;
	char name[32];
	unsigned i;

	printf("# rtw8822c PHY and channel reference trace, FreeBSD a143197 rtw88\n");
	printf("# crystal_cap 0x3c, antennas AB, 20 MHz, regd unset\n");
	printf("# EFUSE: physical byte i = (i * 29 + 7) & 0x7f; blank = 0xff\n");
	for (i = 0; i < 2; i++) {
		unsigned b;

		for (b = 0; b < sizeof(efuse_phys); b++)
			efuse_phys[b] = i == 0 ? (u8)((b * 29 + 7) & 0x7f) : 0xff;
		setup(&dev);
		mark_tables = mark_rf_init = ~0U;
		rtw8822c_phy_set_param(&dev);
		if (mark_tables == ~0U || mark_rf_init == ~0U) {
			fprintf(stderr, "phy_set_param markers missing\n");
			return 1;
		}
		if (i == 0)
			section("phy-pre", 0, mark_tables);
		section(i == 0 ? "phy-post" : "phy-post-blank", mark_tables,
		    model_ntrace);
		printf("# DAC calibration marker after write %u\n",
		    mark_rf_init - mark_tables);
	}
	for (i = 0; i < sizeof(channels); i++) {
		setup(&dev);
		rtw8822c_set_channel(&dev, channels[i], RTW_CHANNEL_WIDTH_20,
		    RTW_SC_DONT_CARE);
		snprintf(name, sizeof(name), "channel-%u", channels[i]);
		section(name, 0, model_ntrace);
	}
	return 0;
}
