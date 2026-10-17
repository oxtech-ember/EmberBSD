/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: host boundary for unchanged pinned PHY table load, AI-assisted. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
typedef uint8_t u8;
#define BIT(n) (1U << (n))
struct rtw_table { unsigned id; };
struct rtw_rfe_def { const struct rtw_table *agc_btg_tbl; };
struct rtw_chip_info {
	const struct rtw_table *mac_tbl, *bb_tbl, *agc_tbl, *rfk_init_tbl;
	const struct rtw_table *rf_tbl[2];
};
struct rtw_dev {
	const struct rtw_chip_info *chip;
	struct { struct rtw_dpk_info { bool is_dpk_pwr_on; } dpk_info; } dm_info;
	struct { unsigned rf_path_num; } hal;
};
static const struct rtw_rfe_def *
rtw_get_rfe_def(struct rtw_dev *dev)
{
	static const struct rtw_rfe_def rfe = { NULL };

	(void)dev;
	return &rfe; /* RTL8822C RFE5 has no additional BTG AGC table. */
}
static void
rtw_load_table(struct rtw_dev *dev, const struct rtw_table *table)
{

	(void)dev;
	printf("%u 0 0 0\n", table->id);
}
static void
rtw_write32_mask(struct rtw_dev *dev, unsigned addr, unsigned mask, unsigned value)
{

	(void)dev;
	printf("0 %x %x %x\n", addr, mask, value);
}
#include "body.c"
int
main(void)
{
	const struct rtw_table tables[] = { {1}, {2}, {3}, {4}, {5}, {6} };
	const struct rtw_chip_info chip = {
		&tables[0], &tables[1], &tables[2], &tables[3],
		{ &tables[4], &tables[5] }
	};
	struct rtw_dev dev = { .chip = &chip, .hal = {2} };

	rtw_phy_load_tables(&dev);
	return dev.dm_info.dpk_info.is_dpk_pwr_on ? 0 : 1;
}
