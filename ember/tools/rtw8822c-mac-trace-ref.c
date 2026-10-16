/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD RTL8822C MAC init reference harness, AI-assisted. */
/*
 * Minimal host environment for the unchanged reference rtw88 MAC init
 * functions. Built only by rtw8822c-mac-trace-gen.sh; the extracted
 * reference body is included at REF_BODY and is not redistributed here.
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
#define BIT(n) (1U << (n))
#define GENMASK(h, l) (((~0U) << (l)) & (~0U >> (31 - (h))))
#define EXPORT_SYMBOL(x)
#define rtw_err(dev, ...) fprintf(stderr, __VA_ARGS__)
#define __ffs(x) ((u32)__builtin_ctz(x))

#include "reg.h"
/* mac.h at the pinned revision; the header itself needs the full rtw_dev. */
#define C2H_PKT_BUF 256
#define REPORT_BUF 128
#define PHY_STATUS_SIZE 4
#define RSVD_PG_H2C_EXTRAINFO_NUM 24
#define RSVD_PG_H2C_STATICINFO_NUM 8
#define RSVD_PG_H2CQ_NUM 8
#define RSVD_PG_CPU_INSTRUCTION_NUM 0
#define RSVD_PG_FW_TXBUF_NUM 4
/* bf.h at the pinned revision. */
#define REG_SND_PTCL_CTRL 0x0718
#define BIT_DIS_CHK_VHTSIGB_CRC BIT(6)
/* SDIO-only registers; the PCIe trace never reaches them. */
#define REG_SDIO_FREE_TXPG 0x10250000
#define REG_SDIO_TX_CTRL 0x10250000
#define TX_PAGE_SIZE_SHIFT 7
#define TX_PAGE_SIZE (1 << TX_PAGE_SIZE_SHIFT)

enum rtw_hci_type { RTW_HCI_TYPE_PCIE, RTW_HCI_TYPE_USB, RTW_HCI_TYPE_SDIO };
enum rtw_dma_mapping {
	RTW_DMA_MAPPING_EXTRA = 0, RTW_DMA_MAPPING_LOW = 1,
	RTW_DMA_MAPPING_NORMAL = 2, RTW_DMA_MAPPING_HIGH = 3
};
enum { RTW_WCPU_3081, RTW_WCPU_8051 };
enum { RTW_CHIP_TYPE_8822B, RTW_CHIP_TYPE_8822C };
enum { RTW_CHIP_VER_CUT_D = 3 };

struct rtw_rqpn {
	enum rtw_dma_mapping dma_map_vo, dma_map_vi, dma_map_be, dma_map_bk,
	    dma_map_mg, dma_map_hi;
};
struct rtw_page_table { u16 hq_num, nq_num, lq_num, exq_num, gapq_num; };
struct rtw_fifo_conf {
	u16 rsvd_boundary, rsvd_pg_num, rsvd_drv_pg_num, txff_pg_num,
	    acq_pg_num, rsvd_drv_addr, rsvd_h2c_info_addr,
	    rsvd_h2c_sta_info_addr, rsvd_h2cq_addr, rsvd_cpu_instr_addr,
	    rsvd_fw_txbuf_addr, rsvd_csibuf_addr;
	const struct rtw_rqpn *rqpn;
};
struct rtw_chip_info {
	int id, wlan_cpu;
	u32 txff_size, rxff_size, page_size;
	u16 rsvd_drv_pg_num;
	u8 csi_buf_pg_num, usb_tx_agg_desc_num;
	const struct rtw_page_table *page_table;
	const struct rtw_rqpn *rqpn_table;
};
struct rtw_dev {
	const struct rtw_chip_info *chip;
	struct rtw_fifo_conf fifo;
	struct { enum rtw_hci_type type; int bulkout_num; } hci;
	struct { u8 cut_version; } hal;
};

/* rtw8822c.c rqpn_table_8822c[1] and page_table_8822c[1]: PCIe. */
static const struct rtw_rqpn rqpn_8822c[2] = {
	{ 0 },
	{ RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_NORMAL, RTW_DMA_MAPPING_LOW,
	  RTW_DMA_MAPPING_LOW, RTW_DMA_MAPPING_EXTRA, RTW_DMA_MAPPING_HIGH },
};
static const struct rtw_page_table page_8822c[2] = {
	{ 0 }, { 64, 64, 64, 64, 1 },
};
static const struct rtw_chip_info chip_8822c = {
	.id = RTW_CHIP_TYPE_8822C, .wlan_cpu = RTW_WCPU_3081,
	.txff_size = 262144, .rxff_size = 24576, .page_size = TX_PAGE_SIZE,
	.rsvd_drv_pg_num = 16, .csi_buf_pg_num = 50,
	.page_table = page_8822c, .rqpn_table = rqpn_8822c,
};

static u8 rtw_read8(struct rtw_dev *d, u32 a) { (void)d; return (u8)model_read(a, 1); }
static u16 rtw_read16(struct rtw_dev *d, u32 a) { (void)d; return (u16)model_read(a, 2); }
static u32 rtw_read32(struct rtw_dev *d, u32 a) { (void)d; return model_read(a, 4); }
static void rtw_write8(struct rtw_dev *d, u32 a, u8 v) { (void)d; model_write(a, 1, v); }
static void rtw_write16(struct rtw_dev *d, u32 a, u16 v) { (void)d; model_write(a, 2, v); }
static void rtw_write32(struct rtw_dev *d, u32 a, u32 v) { (void)d; model_write(a, 4, v); }
static void rtw_write8_set(struct rtw_dev *d, u32 a, u8 b) { rtw_write8(d, a, rtw_read8(d, a) | b); }
static void rtw_write8_clr(struct rtw_dev *d, u32 a, u8 b) { rtw_write8(d, a, rtw_read8(d, a) & ~b); }
static void rtw_write32_set(struct rtw_dev *d, u32 a, u32 b) { rtw_write32(d, a, rtw_read32(d, a) | b); }
static void rtw_write32_clr(struct rtw_dev *d, u32 a, u32 b) { rtw_write32(d, a, rtw_read32(d, a) & ~b); }
static void
rtw_write32_mask(struct rtw_dev *d, u32 a, u32 mask, u32 data)
{
	u32 orig = rtw_read32(d, a);

	rtw_write32(d, a, (orig & ~mask) | ((data << __ffs(mask)) & mask));
}
static void
rtw_write8_mask(struct rtw_dev *d, u32 a, u32 mask, u8 data)
{
	u8 orig = rtw_read8(d, a);

	mask &= 0xff;
	rtw_write8(d, a, (u8)((orig & ~mask) | ((data << __ffs(mask)) & mask)));
}
static bool
check_hw_ready(struct rtw_dev *d, u32 a, u32 mask, u32 target)
{
	return (rtw_read32(d, a) & mask) == (target ? mask : 0);
}
static bool rtw_chip_wcpu_8051(struct rtw_dev *d) { return d->chip->wlan_cpu == RTW_WCPU_8051; }
static bool rtw_chip_wcpu_3081(struct rtw_dev *d) { return d->chip->wlan_cpu == RTW_WCPU_3081; }
static enum rtw_hci_type rtw_hci_type(struct rtw_dev *d) { return d->hci.type; }

#include REF_BODY

int
main(void)
{
	static const u8 cuts[] = { 2, 3 };
	unsigned c, i;

	printf("# rtw8822c MAC init reference trace, FreeBSD a143197 rtw88\n");
	for (c = 0; c < sizeof(cuts); c++) {
		struct rtw_dev dev = { .chip = &chip_8822c };

		dev.hci.type = RTW_HCI_TYPE_PCIE;
		dev.hal.cut_version = cuts[c];
		model_reset();
		if (rtw_init_trx_cfg(&dev) != 0 || rtw8822c_mac_init(&dev) != 0 ||
		    rtw_drv_info_cfg(&dev) != 0) {
			fprintf(stderr, "reference init failed\n");
			return 1;
		}
		rtw_pci_interface_cfg(&dev);
		printf("cut %u writes %u\n", cuts[c], model_ntrace);
		for (i = 0; i < model_ntrace; i++)
			printf("%s\n", model_trace[i]);
	}
	return 0;
}
