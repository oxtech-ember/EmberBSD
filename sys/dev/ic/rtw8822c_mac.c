/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: FreeBSD a143197801c680b958ab662c88cf086668a1958b rtw88 MAC init. */
/*
 * Native adaptation of mac.c rtw_init_trx_cfg/rtw_drv_info_cfg,
 * rtw8822c.c rtw8822c_mac_init and pci.c rtw_pci_interface_cfg for
 * RTL8822C on PCIe. Values were taken from the pinned headers by the
 * compiler; ember/tools/rtw8822c-mac.trace holds the reference writes.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>

/* rtw8822c_hw_spec and mac.h, PCIe profile. */
#define TXFF_PAGES		(262144 / 128)
#define RXFF_SIZE		24576
#define C2H_PKT_BUF		256
#define RSVD_DRV_PAGES		16
#define RSVD_H2C_EXTRA_PAGES	24
#define RSVD_H2C_STATIC_PAGES	8
#define RSVD_H2CQ_PAGES		8
#define RSVD_FW_TXBUF_PAGES	4
#define CSI_BUF_PAGES		50
#define RSVD_PAGES		(RSVD_DRV_PAGES + RSVD_H2C_EXTRA_PAGES + \
				 RSVD_H2C_STATIC_PAGES + RSVD_H2CQ_PAGES + \
				 RSVD_FW_TXBUF_PAGES + CSI_BUF_PAGES)
/* page_table_8822c[1]: HQ, NQ, LQ, EXQ, GAP. */
#define PG_HQ			64
#define PG_NQ			64
#define PG_LQ			64
#define PG_EXQ			64
#define PG_GAP			1
#define TX_PAGE_SHIFT		7
#define CUT_D			3
#define POLL_TRIES		1000
#define POLL_US			10

struct mac_io {
	struct rtw88_softc *sc;
	int error;
};

static uint32_t
rd(struct mac_io *io, uint32_t addr, unsigned size)
{
	const struct rtw88_bus_ops *ops = io->sc->sc_ops;
	uint32_t v32 = 0;
	uint16_t v16 = 0;
	uint8_t v8 = 0;

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

/* txdma_queue_mapping: rqpn_table_8822c[1]. */
static void
queue_mapping(struct mac_io *io)
{
	/* HIQ high, MGQ extra, BKQ low, BEQ low, VIQ normal, VOQ normal. */
	w16(io, 0x010c, 3 << 14 | 0 << 12 | 1 << 10 | 1 << 8 | 2 << 6 | 2 << 4);
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

/* rtw8822c_mac_init. */
static void
chip_mac(struct mac_io *io)
{
	uint32_t v;

	/* TX queue control and SIFS. */
	v = rd(io, 0x0420, 1);
	w8(io, 0x0420, v | 0x80);
	w8(io, 0x0421, 0x1f);			/* WLAN_TXQ_RPT_EN */
	w16(io, 0x0428, 0x100a);		/* SPEC_SIFS */
	w32(io, 0x0514, UINT32_C(0x100a0e0a));	/* SIFS */
	w16(io, 0x063c, 0x0a0a);		/* RESP_SIFS_CCK */
	w16(io, 0x063e, 0x0e0e);		/* RESP_SIFS_OFDM */
	/* Rate fallback. */
	w32(io, 0x0430, UINT32_C(0x01000000));	/* DARFRC */
	w32(io, 0x0434, UINT32_C(0x08070504));	/* DARFRCH */
	w32(io, 0x043c, UINT32_C(0x08070504));	/* RARFRCH */
	w32(io, 0x0444, UINT32_C(0xfe01f010));	/* ARFR0 */
	w32(io, 0x0448, UINT32_C(0x40000000));	/* ARFRH0 */
	w32(io, 0x044c, UINT32_C(0x003ff010));	/* ARFR1_V1 */
	w32(io, 0x0450, UINT32_C(0x40000000));	/* ARFRH1_V1 */
	w32(io, 0x049c, UINT32_C(0x0600f010));	/* ARFR4 */
	w32(io, 0x04a0, UINT32_C(0x400003e0));	/* ARFRH4 */
	w32(io, 0x04a4, UINT32_C(0x0600f015));	/* ARFR5 */
	w32(io, 0x04a8, UINT32_C(0x000000e0));	/* ARFRH5 */
	/* Protocol. */
	w8(io, 0x0455, 0x70);			/* AMPDU_MAX_TIME_V1 */
	set8(io, 0x045e, 0x04);			/* TX_HANG_CTRL: EN_EOF_V1 */
	w8(io, 0x04e5, 0xe0);			/* PRECNT_CTRL: 0x1e0 | EN_PRECNT */
	w8(io, 0x04e6, 0x09);
	w32(io, 0x04c8, UINT32_C(0x3f3f08ff));	/* PROT_MODE_CTRL */
	w16(io, 0x04ce, 0x0801);		/* BAR_MODE_CTRL + 2 */
	w8(io, 0x1448, 0x06);			/* FAST_EDCA_VOVI_SETTING */
	w8(io, 0x144a, 0x06);
	w8(io, 0x144c, 0x06);			/* FAST_EDCA_BEBK_SETTING */
	w8(io, 0x144e, 0x06);
	clr8(io, 0x0426, 0x20);			/* LIFETIME_EN: BA_PARSER_EN */
	clr32(io, 0x0440, UINT32_C(0x00600000));	/* RRSR: RRSR_RSC */
	/* EDCA. */
	w32(io, 0x0500, UINT32_C(0x002fa226));
	w32(io, 0x0504, UINT32_C(0x005ea328));
	w32(io, 0x0508, UINT32_C(0x005ea42b));
	w32(io, 0x050c, UINT32_C(0x0000a44f));
	w8(io, 0x0512, 0x1c);			/* PIFS */
	clr8(io, 0x0521, 0x10);			/* TX_PTCL_CTRL: SIFS_BK_EN */
	set8(io, 0x0525, 0x07);			/* RD_CTRL: DIS_*_CFE */
	/* MAC clock. */
	clr32(io, 0x0024, UINT32_C(0x00300000));	/* AFE_CTRL1: MAC_CLK_SEL */
	w8(io, 0x055c, 0x50);			/* USTIME_TSF */
	w8(io, 0x0638, 0x50);			/* USTIME_EDCA */
	set8(io, 0x0577, 0x0b);			/* MISC_CTRL */
	clr8(io, 0x05b4, 0x70);			/* TIMER0_SRC_SEL */
	w16(io, 0x0522, 0);			/* TXPAUSE */
	w8(io, 0x051b, 0x09);			/* SLOT */
	w32(io, 0x0544, UINT32_C(0x001b0005));	/* RD_NAV_NXT */
	w16(io, 0x055e, 0x3030);		/* RXTSF_OFFSET_CCK */
	set8(io, 0x0550, 0x08);			/* BCN_CTRL: EN_BCN_FUNCTION */
	w32(io, 0x0540, UINT32_C(0x00006404));	/* TBTT_PROHIBIT */
	w8(io, 0x0558, 0x04);			/* DRVERLYINT */
	w8(io, 0x0551, 0x10);			/* BCN_CTRL_CLINT0 */
	w8(io, 0x0559, 0x02);			/* BCNDMATIM */
	w8(io, 0x055d, 0xff);			/* BCN_MAX_ERR */
	/* WMAC. */
	w32(io, 0x0620, UINT32_C(0xffffffff));	/* MAR */
	w32(io, 0x0624, UINT32_C(0xffffffff));
	w8(io, 0x06de, 0x84);			/* BBPSF_CTRL + 2 */
	w8(io, 0x0640, 0x21);			/* ACKTO */
	w8(io, 0x0639, 0x6a);			/* ACKTO_CCK */
	w16(io, 0x0642, 0x0040);		/* EIFS */
	w8(io, 0x0652, 0xc8);			/* NAV_CTRL + 2 */
	w8(io, 0x066e, 0x05);			/* WMAC_TRXPTCL_CTL_H + 2 */
	w32(io, 0x06a0, UINT32_C(0xffffffff));	/* RXFLTMAP0 */
	w16(io, 0x06a4, 0xffff);		/* RXFLTMAP2 */
	w32(io, 0x0608, UINT32_C(0xe400220e));	/* RCR */
	w8(io, 0x060c, 12288 >> 9);		/* RX_PKT_LIMIT */
	w8(io, 0x0606, 0x30);			/* TCR + 2 */
	w8(io, 0x0605, 0x30);			/* TCR + 1 */
	set32(io, 0x1664, 0x200);		/* GENERAL_OPTION */
	w32(io, 0x07d8, UINT32_C(0xb0810041));	/* WMAC_OPTION_FUNCTION + 8 */
	w8(io, 0x07d4, 0x98);			/* WMAC_OPTION_FUNCTION_1 */
	/* Low power RX filter. */
	v = rd(io, 0x1612, 2) & 0xf00f;		/* RXPSF_CTRL + 2 */
	w16(io, 0x1612, v | 0x0550);		/* RXGCK VHT/HT/OFDM/CCK = 1 */
	w16(io, 0x1610, 0x3f80);		/* PKTLENTHR 1, checks, OFDMRST */
	w32(io, 0x1614, UINT32_C(0xffffffff));	/* RXPSF_TYPE_CTRL */
	v = rd(io, 0x1610, 2);
	v &= ~(uint32_t)(0x20 | 0x40 | 0x10);	/* MHCHKEN, CCKRST, CONT_ERR */
	w16(io, 0x1610, (v & ~(uint32_t)0x7) | 0x7);	/* ERRTHR 7 */
	set8(io, 0x0718, 0x40);			/* SND_PTCL_CTRL */
	w32(io, 0x0304, UINT32_C(0x33330000));	/* INT_MIG */
}

/* rtw_drv_info_cfg and rtw_pci_interface_cfg. */
static void
drv_info(struct mac_io *io)
{
	uint32_t v;

	w8(io, 0x060f, 4);			/* RX_DRVINFO_SZ: PHY_STATUS_SIZE */
	v = rd(io, 0x0115, 1);			/* TRXFF_BNDY + 1 */
	w8(io, 0x0115, (v & 0xf0) | 0x0f);
	set32(io, 0x0608, UINT32_C(0x10000000));	/* RCR: APP_PHYSTS */
	clr32(io, 0x07d4, 0x300);		/* WMAC_OPTION_FUNCTION + 4 */
	if (io->sc->sc_cut >= CUT_D) {
		v = rd(io, 0x03fc, 4);		/* HCI_MIX_CFG */
		w32(io, 0x03fc, v | UINT32_C(0x04000000)); /* EMAC_PDN_AUX */
	}
}

int
rtw8822c_mac_init(struct rtw88_softc *sc)
{
	struct mac_io io = { sc, 0 };
	uint16_t h2cq_page;

	queue_mapping(&io);
	priority_queues(&io, &h2cq_page);
	h2c_queue(&io, h2cq_page);
	chip_mac(&io);
	drv_info(&io);
	return io.error;
}
