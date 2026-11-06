/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 MAC init. */
/*
 * Native adaptation of mac.c rtw_init_trx_cfg/rtw_drv_info_cfg,
 * rtw8821c.c rtw8821c_mac_init and pci.c rtw_pci_interface_cfg for
 * RTL8821C on PCIe. Register definitions follow the pinned BSD donor.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8821c_boot.h>

/* rtw8821c_hw_spec and mac.h, PCIe profile. */
#define TXFF_PAGES		(65536 / 128)
#define RXFF_SIZE		16384
#define C2H_PKT_BUF		256
#define RSVD_DRV_PAGES		8
#define RSVD_H2C_EXTRA_PAGES	24
#define RSVD_H2C_STATIC_PAGES	8
#define RSVD_H2CQ_PAGES		8
#define RSVD_FW_TXBUF_PAGES	4
#define CSI_BUF_PAGES		0
#define RSVD_PAGES		(RSVD_DRV_PAGES + RSVD_H2C_EXTRA_PAGES + \
				 RSVD_H2C_STATIC_PAGES + RSVD_H2CQ_PAGES + \
				 RSVD_FW_TXBUF_PAGES + CSI_BUF_PAGES)
/* page_table_8821c[1]: HQ, NQ, LQ, EXQ, GAP. */
#define PG_HQ			16
#define PG_NQ			16
#define PG_LQ			16
#define PG_EXQ			14
#define PG_GAP			1
#define TX_PAGE_SHIFT		7
#define CUT_D			3
#define POLL_TRIES		1000
#define POLL_US			10

struct mac_io {
	struct rtw88_softc *sc;
	uint32_t generation;
	int error;
};

static uint32_t
rd(struct mac_io *io, uint32_t addr, unsigned size)
{
	const struct rtw88_bus_ops *ops = io->sc->sc_ops;
	uint32_t v32 = 0;
	uint16_t v16 = 0;
	uint8_t v8 = 0;

	if (io->error == 0 && (io->sc->sc_stop_requested ||
	    io->sc->sc_generation != io->generation))
		io->error = EINTR;
	if (io->error != 0)
		return 0;
	if (size == 1) {
		io->error = ops->read_1(io->sc->sc_bus, addr, &v8);
		return v8;
	}
	if (size == 2) {
		io->error = ops->read_2(io->sc->sc_bus, addr, &v16);
		return v16;
	}
	io->error = ops->read_4(io->sc->sc_bus, addr, &v32);
	return v32;
}

static void
wr(struct mac_io *io, uint32_t addr, unsigned size, uint32_t v)
{
	const struct rtw88_bus_ops *ops = io->sc->sc_ops;

	if (io->error == 0 && (io->sc->sc_stop_requested ||
	    io->sc->sc_generation != io->generation))
		io->error = EINTR;
	if (io->error != 0)
		return;
	if (size == 1)
		io->error = ops->write_1(io->sc->sc_bus, addr, (uint8_t)v);
	else if (size == 2)
		io->error = ops->write_2(io->sc->sc_bus, addr, (uint16_t)v);
	else
		io->error = ops->write_4(io->sc->sc_bus, addr, v);
}

#define w8(io, a, v)	wr(io, a, 1, v)
#define w16(io, a, v)	wr(io, a, 2, v)
#define w32(io, a, v)	wr(io, a, 4, v)

static void
set8(struct mac_io *io, uint32_t addr, uint8_t bits)
{
	uint32_t v = rd(io, addr, 1);

	w8(io, addr, v | bits);
}

static void
clr8(struct mac_io *io, uint32_t addr, uint8_t bits)
{
	uint32_t v = rd(io, addr, 1);

	w8(io, addr, v & ~(uint32_t)bits);
}

static void
set32(struct mac_io *io, uint32_t addr, uint32_t bits)
{
	uint32_t v = rd(io, addr, 4);

	w32(io, addr, v | bits);
}

static void
clr32(struct mac_io *io, uint32_t addr, uint32_t bits)
{
	uint32_t v = rd(io, addr, 4);

	w32(io, addr, v & ~bits);
}

/* check_hw_ready with a bounded budget; ETIMEDOUT keeps the cause. */
static void
wait_clear(struct mac_io *io, uint32_t addr, uint32_t mask)
{
	unsigned i;

	for (i = 0; i < POLL_TRIES && io->error == 0; i++) {
		if ((rd(io, addr, 4) & mask) == 0)
			return;
		io->sc->sc_ops->delay_us(io->sc->sc_bus, POLL_US);
	}
	if (io->error == 0)
		io->error = ETIMEDOUT;
}

/* txdma_queue_mapping: rqpn_table_8821c[1]. */
static void
queue_mapping(struct mac_io *io)
{
	/* HIQ high, MGQ extra, BKQ low, BEQ low, VIQ normal, VOQ normal. */
	w16(io, 0x010c, 3 << 14 | 1 << 10 | 1 << 8 | 2 << 6 | 2 << 4);
	w8(io, 0x0100, 0);
	w8(io, 0x0100, 0xff);			/* MAC_TRX_ENABLE */
	w32(io, 0x1330, UINT32_C(0x80000000));	/* H2CQ_CSR: BIT_H2CQ_FULL */
}

/* rtw_set_trx_fifo_info and __priority_queue_cfg. */
static void
priority_queues(struct mac_io *io, uint16_t *h2cq_page)
{
	const uint16_t boundary = TXFF_PAGES - RSVD_PAGES;
	const uint16_t acq = TXFF_PAGES - RSVD_PAGES;
	const uint16_t pubq = acq - PG_HQ - PG_LQ - PG_NQ - PG_EXQ - PG_GAP;

	*h2cq_page = TXFF_PAGES - CSI_BUF_PAGES - RSVD_FW_TXBUF_PAGES -
	    RSVD_H2CQ_PAGES;
	w16(io, 0x0230, PG_HQ);			/* FIFOPAGE_INFO_1..5 */
	w16(io, 0x0234, PG_LQ);
	w16(io, 0x0238, PG_NQ);
	w16(io, 0x023c, PG_EXQ);
	w16(io, 0x0240, pubq);
	set32(io, 0x022c, UINT32_C(0x80000000));	/* RQPN_CTRL_2: LD_RQPN */
	w16(io, 0x0204, boundary);		/* FIFOPAGE_CTRL_2 */
	set8(io, 0x0422, 0x10);			/* FWHW_TXQ_CTRL: EN_WR_FREE_TAIL */
	w16(io, 0x0424, boundary);		/* BCNQ_BDNY_V1 */
	w16(io, 0x0206, boundary);
	w16(io, 0x0456, boundary);		/* BCNQ1_BDNY_V1 */
	w32(io, 0x011c, RXFF_SIZE - C2H_PKT_BUF - 1);	/* RXFF_BNDY */
	set8(io, 0x0208, 0x01);			/* AUTO_LLT_V1: AUTO_INIT_LLT */
	wait_clear(io, 0x0208, 0x01);
	w8(io, 0x0103, 0);			/* CR + 3 */
}

/* init_h2c. */
static void
h2c_queue(struct mac_io *io, uint16_t h2cq_page)
{
	const uint32_t addr = (uint32_t)h2cq_page << TX_PAGE_SHIFT;
	const uint32_t size = RSVD_H2CQ_PAGES << TX_PAGE_SHIFT;
	uint32_t v, wp, rp, avail;

	v = rd(io, 0x0244, 4);			/* H2C_HEAD */
	w32(io, 0x0244, (v & UINT32_C(0xfffc0000)) | addr);
	v = rd(io, 0x024c, 4);			/* H2C_READ_ADDR */
	w32(io, 0x024c, (v & UINT32_C(0xfffc0000)) | addr);
	v = rd(io, 0x0248, 4);			/* H2C_TAIL */
	w32(io, 0x0248, (v & UINT32_C(0xfffc0000)) | (addr + size));
	v = rd(io, 0x0254, 1);			/* H2C_INFO */
	w8(io, 0x0254, (v & 0xfc) | 0x01);
	v = rd(io, 0x0254, 1);
	w8(io, 0x0254, (v & 0xfb) | 0x04);
	v = rd(io, 0x020d, 1);			/* TXDMA_OFFSET_CHK + 1 */
	w8(io, 0x020d, (v & 0x7f) | 0x80);
	wp = rd(io, 0x10d4, 4) & 0x3ffff;	/* H2C_PKT_WRITEADDR */
	rp = rd(io, 0x10d0, 4) & 0x3ffff;	/* H2C_PKT_READADDR */
	avail = wp >= rp ? size - (wp - rp) : rp - wp;
	if (io->error == 0 && avail != size) {
		printf("rtwce: H2C queue mismatch wp %#x rp %#x\n", wp, rp);
		io->error = EIO;
	}
}

/* Pinned 8821C register definitions and MAC sequence. */
#define BIT(n) (1U << (n))
#define REG_SND_PTCL_CTRL 0x0718
#define BIT_DIS_CHK_VHTSIGB_CRC 0x40
#define BIT_EN_BCN_FUNCTION	BIT(3)
#define BIT_EN_EOF_V1		BIT(2)
#define BIT_EN_PRECNT		BIT(11)
#define BIT_SIFS_BK_EN		BIT(12)
#define BIT_TSFT_SEL_TIMER0	(BIT(4) | BIT(5) | BIT(6))
#define FAST_EDCA_BE_TH		0x06
#define FAST_EDCA_BK_TH		0x06
#define FAST_EDCA_VI_TH		0x06
#define FAST_EDCA_VO_TH		0x06
#define REG_ACKTO_CCK		0x0639
#define REG_AMPDU_MAX_TIME_V1	0x0455
#define REG_BAR_MODE_CTRL	0x04CC
#define REG_BCNDMATIM		0x0559
#define REG_BCN_CTRL		0x0550
#define REG_DRVERLYINT		0x0558
#define REG_EDCA_VI_PARAM	0x0504
#define REG_EDCA_VO_PARAM	0x0500
#define REG_FAST_EDCA_BEBK_SETTING 0x144C
#define REG_FAST_EDCA_VOVI_SETTING 0x1448
#define REG_INIRTS_RATE_SEL 0x0480
#define REG_PIFS		0x0512
#define REG_PRECNT_CTRL		0x04E5
#define REG_PROT_MODE_CTRL	0x04C8
#define REG_RCR			0x0608
#define REG_RD_NAV_NXT		0x0544
#define REG_RXFLTMAP0		0x06A0
#define REG_RXFLTMAP2		0x06A4
#define REG_RXTSF_OFFSET_CCK	0x055E
#define REG_RX_PKT_LIMIT	0x060C
#define REG_SIFS		0x0514
#define REG_SLOT		0x051B
#define REG_TBTT_PROHIBIT	0x0540
#define REG_TCR			0x0604
#define REG_TIMER0_SRC_SEL	0x05B4
#define REG_TXPAUSE		0x0522
#define REG_TX_HANG_CTRL	0x045E
#define REG_TX_PTCL_CTRL	0x0520
#define REG_WMAC_OPTION_FUNCTION 0x07D0
#define REG_WMAC_TRXPTCL_CTL_H	0x066C
#define WLAN_AMPDU_MAX_TIME		0x70
#define WLAN_BAR_RETRY_LIMIT		0x01
#define WLAN_BCN_DMA_TIME	0x02
#define WLAN_DRV_EARLY_INT	0x04
#define WLAN_MAC_OPT_FUNC2		0xb0810041
#define WLAN_MAC_OPT_NORM_FUNC1		0x98
#define WLAN_MAX_AGG_PKT_LIMIT		0x20
#define WLAN_RDG_NAV		0x05
#define WLAN_TXOP_NAV		0x1B
#define WLAN_NAV_CFG		(WLAN_RDG_NAV | (WLAN_TXOP_NAV << 16))
#define WLAN_PIFS_TIME		0x19
#define WLAN_PRE_TXCNT_TIME_TH		0x1E4
#define WLAN_RA_TRY_RATE_AGG_LIMIT	0x08
#define WLAN_RCR_CFG		0xE400220E
#define WLAN_RTS_LEN_TH			0xFF
#define WLAN_RTS_MAX_AGG_PKT_LIMIT	0x20
#define WLAN_RTS_TX_TIME_TH		0x08
#define WLAN_RXPKT_MAX_SZ	12288
#define WLAN_RXPKT_MAX_SZ_512	(WLAN_RXPKT_MAX_SZ >> 9)
#define WLAN_RX_FILTER0		0x0FFFFFFF
#define WLAN_RX_FILTER2		0xFFFF
#define WLAN_CCK_RX_TSF		0x30
#define WLAN_OFDM_RX_TSF	0x30
#define WLAN_RX_TSF_CFG		(WLAN_CCK_RX_TSF | (WLAN_OFDM_RX_TSF) << 8)
#define WLAN_SIFS_CCK_CONT_TX	0xA
#define WLAN_SIFS_OFDM_CONT_TX	0xE
#define BIT_SHIFT_SIFS_OFDM_CTX	8
#define WLAN_SIFS_CCK_TRX	0x10
#define BIT_SHIFT_SIFS_CCK_TRX	16
#define WLAN_SIFS_OFDM_TRX	0x10
#define BIT_SHIFT_SIFS_OFDM_TRX	24
#define WLAN_SIFS_CFG	(WLAN_SIFS_CCK_CONT_TX | \
			(WLAN_SIFS_OFDM_CONT_TX << BIT_SHIFT_SIFS_OFDM_CTX) | \
			(WLAN_SIFS_CCK_TRX << BIT_SHIFT_SIFS_CCK_TRX) | \
			(WLAN_SIFS_OFDM_TRX << BIT_SHIFT_SIFS_OFDM_TRX))
#define WLAN_SLOT_TIME		0x09
#define WLAN_TBTT_PROHIBIT	0x04
#define WLAN_TBTT_HOLD_TIME	0x064
#define BIT_SHIFT_TBTT_HOLD_TIME_AP 8
#define WLAN_TBTT_TIME	(WLAN_TBTT_PROHIBIT |\
			(WLAN_TBTT_HOLD_TIME << BIT_SHIFT_TBTT_HOLD_TIME_AP))
#define WLAN_TX_FUNC_CFG1		0x30
#define WLAN_TX_FUNC_CFG2		0x30
#define WLAN_VI_TXOP_LIMIT	0x3BC
#define WLAN_VO_TXOP_LIMIT	0x186
static void
chip_mac(struct mac_io *io)
{
	uint32_t value32;
	uint16_t pre_txcnt;

	/* protocol configuration */
	w8(io, REG_AMPDU_MAX_TIME_V1, WLAN_AMPDU_MAX_TIME);
	set8(io, REG_TX_HANG_CTRL, BIT_EN_EOF_V1);
	pre_txcnt = WLAN_PRE_TXCNT_TIME_TH | BIT_EN_PRECNT;
	w8(io, REG_PRECNT_CTRL, (uint8_t)(pre_txcnt & 0xFF));
	w8(io, REG_PRECNT_CTRL + 1, (uint8_t)(pre_txcnt >> 8));
	value32 = WLAN_RTS_LEN_TH | (WLAN_RTS_TX_TIME_TH << 8) |
		  (WLAN_MAX_AGG_PKT_LIMIT << 16) |
		  (WLAN_RTS_MAX_AGG_PKT_LIMIT << 24);
	w32(io, REG_PROT_MODE_CTRL, value32);
	w16(io, REG_BAR_MODE_CTRL + 2,
		    WLAN_BAR_RETRY_LIMIT | WLAN_RA_TRY_RATE_AGG_LIMIT << 8);
	w8(io, REG_FAST_EDCA_VOVI_SETTING, FAST_EDCA_VO_TH);
	w8(io, REG_FAST_EDCA_VOVI_SETTING + 2, FAST_EDCA_VI_TH);
	w8(io, REG_FAST_EDCA_BEBK_SETTING, FAST_EDCA_BE_TH);
	w8(io, REG_FAST_EDCA_BEBK_SETTING + 2, FAST_EDCA_BK_TH);
	set8(io, REG_INIRTS_RATE_SEL, BIT(5));

	/* EDCA configuration */
	clr8(io, REG_TIMER0_SRC_SEL, BIT_TSFT_SEL_TIMER0);
	w16(io, REG_TXPAUSE, 0);
	w8(io, REG_SLOT, WLAN_SLOT_TIME);
	w8(io, REG_PIFS, WLAN_PIFS_TIME);
	w32(io, REG_SIFS, WLAN_SIFS_CFG);
	w16(io, REG_EDCA_VO_PARAM + 2, WLAN_VO_TXOP_LIMIT);
	w16(io, REG_EDCA_VI_PARAM + 2, WLAN_VI_TXOP_LIMIT);
	w32(io, REG_RD_NAV_NXT, WLAN_NAV_CFG);
	w16(io, REG_RXTSF_OFFSET_CCK, WLAN_RX_TSF_CFG);

	/* Set beacon cotnrol - enable TSF and other related functions */
	set8(io, REG_BCN_CTRL, BIT_EN_BCN_FUNCTION);

	/* Set send beacon related registers */
	w32(io, REG_TBTT_PROHIBIT, WLAN_TBTT_TIME);
	w8(io, REG_DRVERLYINT, WLAN_DRV_EARLY_INT);
	w8(io, REG_BCNDMATIM, WLAN_BCN_DMA_TIME);
	clr8(io, REG_TX_PTCL_CTRL + 1, BIT_SIFS_BK_EN >> 8);

	/* WMAC configuration */
	w32(io, REG_RXFLTMAP0, WLAN_RX_FILTER0);
	w16(io, REG_RXFLTMAP2, WLAN_RX_FILTER2);
	w32(io, REG_RCR, WLAN_RCR_CFG);
	w8(io, REG_RX_PKT_LIMIT, WLAN_RXPKT_MAX_SZ_512);
	w8(io, REG_TCR + 2, WLAN_TX_FUNC_CFG2);
	w8(io, REG_TCR + 1, WLAN_TX_FUNC_CFG1);
	w8(io, REG_ACKTO_CCK, 0x40);
	set8(io, REG_WMAC_TRXPTCL_CTL_H, BIT(1));
	set8(io, REG_SND_PTCL_CTRL,
		       BIT_DIS_CHK_VHTSIGB_CRC);
	w32(io, REG_WMAC_OPTION_FUNCTION + 8, WLAN_MAC_OPT_FUNC2);
	w8(io, REG_WMAC_OPTION_FUNCTION + 4, WLAN_MAC_OPT_NORM_FUNC1);

}


static void
drv_info(struct mac_io *io)
{
	uint32_t v;

	w8(io, 0x060f, 4);			/* RX_DRVINFO_SZ: PHY_STATUS_SIZE */
	v = rd(io, 0x0115, 1);			/* TRXFF_BNDY + 1 */
	w8(io, 0x0115, (v & 0xf0) | 0x0f);
	set32(io, 0x0608, UINT32_C(0x10000000));	/* RCR: APP_PHYSTS */
	clr32(io, 0x07d4, 0x300);		/* WMAC_OPTION_FUNCTION + 4 */
}

int
rtw8821c_mac_init(struct rtw88_softc *sc)
{
	struct mac_io io;
	uint16_t h2cq_page;

	if (sc == NULL || sc->sc_ops == NULL ||
	    sc->sc_ops->read_1 == NULL || sc->sc_ops->read_2 == NULL ||
	    sc->sc_ops->read_4 == NULL || sc->sc_ops->write_1 == NULL ||
	    sc->sc_ops->write_2 == NULL || sc->sc_ops->write_4 == NULL ||
	    sc->sc_ops->delay_us == NULL)
		return EINVAL;
	io = (struct mac_io){ sc, sc->sc_generation, 0 };
	queue_mapping(&io);
	priority_queues(&io, &h2cq_page);
	h2c_queue(&io, h2cq_page);
	chip_mac(&io);
	drv_info(&io);
	if (io.error == 0 && (sc->sc_stop_requested ||
	    sc->sc_generation != io.generation))
		return EINTR;
	return io.error;
}
