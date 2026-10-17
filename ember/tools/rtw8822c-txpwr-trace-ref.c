/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C TX power reference harness, AI-assisted. */
/*
 * Minimal host environment for the unchanged reference TX power code.
 * Built only by rtw8822c-txpwr-trace-gen.sh; the extracted reference
 * text (defs.h, types.h, body.c) is not redistributed here. Tables are
 * the EmberBSD copy in sys/dev/ic/rtw8822c_table.c.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rtw8822c-mac-model.h"
#include "rtw8822c_table.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
#define BIT(n) (1U << (n))
#define BIT_ULL(n) (1ULL << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define __ffs(x) ((u32)__builtin_ctz(x))
#define __LITTLE_ENDIAN 1234
#define __packed __attribute__((packed))
#define fallthrough __attribute__((fallthrough))
#define EXPORT_SYMBOL(x)
#define WARN_ON(c) (c)
#define WARN(c, ...) (c)
#define rtw_err(dev, ...) fprintf(stderr, __VA_ARGS__)
#define rtw_warn(dev, ...) fprintf(stderr, __VA_ARGS__)
#define rtw_dbg(dev, ...) do { } while (0)
#define msleep(x) do { } while (0)
#define mutex_lock(m) do { } while (0)
#define mutex_unlock(m) do { } while (0)
#define min_t(t, a, b) ((t)(a) < (t)(b) ? (t)(a) : (t)(b))
#define min(a, b) ((a) < (b) ? (a) : (b))
#define min3(a, b, c) min(min(a, b), c)
#define clamp_t(t, v, lo, hi) \
	((t)(v) < (t)(lo) ? (t)(lo) : (t)(v) > (t)(hi) ? (t)(hi) : (t)(v))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Linux enum nl80211_band. */
#define NL80211_BAND_2GHZ 0
#define NL80211_BAND_5GHZ 1
#define NL80211_BAND_60GHZ 2
#define NL80211_BAND_6GHZ 3
#include "reg.h"
#include "defs.h"
#include "types.h"

struct rtw_table { const void *data; u32 size; };
struct rtw_sar_arg { u8 sar_band, path, rs; };
struct rtw_dev;
struct rtw_chip_ops { void (*set_tx_power_index)(struct rtw_dev *); };
struct rtw_chip_info {
	u8 txgi_factor, max_power_index;
	bool is_pwr_by_rate_dec, en_dis_dpd;
	u16 dpd_ratemask;
	const struct rtw_chip_ops *ops;
};
struct rtw_hal {
	u8 rf_path_num;
	u8 current_band_type, current_band_width, current_channel;
	u8 cch_by_bw[RTW_MAX_CHANNEL_WIDTH + 1];
	s8 tx_pwr_by_rate_offset_2g[RTW_RF_PATH_MAX][DESC_RATE_MAX];
	s8 tx_pwr_by_rate_offset_5g[RTW_RF_PATH_MAX][DESC_RATE_MAX];
	s8 tx_pwr_by_rate_base_2g[RTW_RF_PATH_MAX][RTW_RATE_SECTION_NUM];
	s8 tx_pwr_by_rate_base_5g[RTW_RF_PATH_MAX][RTW_RATE_SECTION_NUM];
	s8 tx_pwr_limit_2g[RTW_REGD_MAX][RTW_CHANNEL_WIDTH_MAX]
	    [RTW_RATE_SECTION_NUM][RTW_MAX_CHANNEL_NUM_2G];
	s8 tx_pwr_limit_5g[RTW_REGD_MAX][RTW_CHANNEL_WIDTH_MAX]
	    [RTW_RATE_SECTION_NUM][RTW_MAX_CHANNEL_NUM_5G];
	u8 tx_pwr_tbl[RTW_RF_PATH_MAX][DESC_RATE_MAX];
	enum rtw_sar_bands sar_band;
	int tx_power_mutex;
};
struct rtw_regulatory { u8 txpwr_regd_2g, txpwr_regd_5g; };
struct rtw_dev {
	const struct rtw_chip_info *chip;
	struct rtw_hal hal;
	struct { struct rtw_txpwr_idx txpwr_idx_table[4]; } efuse;
	struct { const struct rtw_regulatory *regulatory; } regd;
	struct rtw_dm_info {
		s8 txagc_remnant_cck;
		s8 txagc_remnant_ofdm[RTW_RF_PATH_MAX];
	} dm_info;
};

static u32 rtw_read32(struct rtw_dev *d, u32 a) { (void)d; return model_read(a, 4); }
static void rtw_write32(struct rtw_dev *d, u32 a, u32 v) { (void)d; model_write(a, 4, v); }
static void
rtw_write32_mask(struct rtw_dev *d, u32 a, u32 mask, u32 data)
{
	u32 orig = rtw_read32(d, a);

	rtw_write32(d, a, (orig & ~mask) | ((data << __ffs(mask)) & mask));
}
/* No SAR source is configured (sar.c rtw_query_sar). */
static s8
rtw_query_sar(struct rtw_dev *d, const struct rtw_sar_arg *arg)
{
	(void)arg;
	return (s8)d->chip->max_power_index;
}
static u8 rtw_regd_get(struct rtw_dev *d);
static u8 bcd_to_dec_pwr_by_rate(u32 hex, u8 i) { (void)hex; (void)i; return 0; }
static void rtw8822c_set_tx_power_index(struct rtw_dev *);

#include "body.c"

static u8
rtw_regd_get(struct rtw_dev *d)
{
	return d->hal.current_band_type == RTW_BAND_2G ?
	    d->regd.regulatory->txpwr_regd_2g :
	    d->regd.regulatory->txpwr_regd_5g;
}

static const struct rtw_chip_ops ops_8822c = {
	.set_tx_power_index = rtw8822c_set_tx_power_index,
};
static const struct rtw_chip_info chip_8822c = {
	.txgi_factor = 2, .max_power_index = 0x7f,
	.is_pwr_by_rate_dec = false, .en_dis_dpd = true,
	.dpd_ratemask = DIS_DPD_RATEALL, .ops = &ops_8822c,
};

static struct rtw_dev dev;

static void
load(const struct rtw8822c_table *tbl, int pg)
{
	struct rtw_table t = { .data = tbl->data, .size = tbl->count };

	if (pg)
		rtw_parse_tbl_bb_pg(&dev, &t);
	else
		rtw_parse_tbl_txpwr_lmt(&dev, &t);
}

/* rtw_chip_parameter_setup order, then one channel at 20 MHz. */
static void
run(u8 regd, u8 rfe, u8 ch, const char *name)
{
	struct rtw_regulatory reg = { regd, regd };
	unsigned i, p;

	memset(&dev, 0, sizeof(dev));
	dev.chip = &chip_8822c;
	dev.hal.rf_path_num = 2;
	dev.regd.regulatory = &reg;
	/* EFUSE TX power image: byte i of path p = (p * 42 + i) * 13 + 5. */
	for (p = 0; p < 4; p++)
		for (i = 0; i < sizeof(dev.efuse.txpwr_idx_table[p]); i++)
			((u8 *)&dev.efuse.txpwr_idx_table[p])[i] =
			    (u8)((p * 42 + i) * 13 + 5);
	rtw_phy_init_tx_power(&dev);
	load(&rtw8822c_bb_pg_type0_tbl, 1);
	load(rfe == 5 ? &rtw8822c_txpwr_lmt_type5_tbl :
	    &rtw8822c_txpwr_lmt_type0_tbl, 0);
	rtw_phy_tx_power_by_rate_config(&dev.hal);
	rtw_phy_tx_power_limit_config(&dev.hal);
	dev.hal.current_band_type = ch <= 14 ? RTW_BAND_2G : RTW_BAND_5G;
	dev.hal.current_band_width = RTW_CHANNEL_WIDTH_20;
	dev.hal.current_channel = ch;
	dev.hal.cch_by_bw[RTW_CHANNEL_WIDTH_20] = ch;
	dev.hal.sar_band = ch <= 14 ? RTW_SAR_BAND_0 : RTW_SAR_BAND_1;
	model_reset();
	rtw_phy_set_tx_power_level(&dev, ch);
	printf("section %s writes %u\n", name, model_ntrace);
	for (i = 0; i < model_ntrace; i++)
		printf("%s\n", model_trace[i]);
}

int
main(void)
{
	static const u8 channels[] = { 1, 6, 11, 13, 14 };
	char name[48];
	unsigned i;

	printf("# rtw8822c TX power reference trace, FreeBSD a143197 rtw88\n");
	printf("# EFUSE txpwr byte i of path p = (p * 42 + i) * 13 + 5, 20 MHz\n");
	for (i = 0; i < sizeof(channels); i++) {
		snprintf(name, sizeof(name), "etsi-rfe0-ch%u", channels[i]);
		run(RTW_REGD_ETSI, 0, channels[i], name);
	}
	run(RTW_REGD_FCC, 0, 6, "fcc-rfe0-ch6");
	run(RTW_REGD_WW, 0, 6, "ww-rfe0-ch6");
	run(RTW_REGD_ETSI, 5, 6, "etsi-rfe5-ch6");
	run(RTW_REGD_UKRAINE, 0, 6, "ukraine-rfe0-ch6");
	return 0;
}
