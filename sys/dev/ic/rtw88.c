/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8822CE resource lifecycle, AI-assisted. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#ifdef _KERNEL
#include <sys/bus.h>
#include <sys/null.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw8822c.h>
#include <dev/ic/rtw8822c_io.h>

#define RTW88_FW_DRV		"rtwce"
#define RTW88_FW_IMG		"rtw8822c_fw.bin"
#define RTW88_REG_MCUFW_CTRL	0x0080
#define RTW88_MCUFWDL_EN	0x0001
#define RTW88_FW_DW_RDY		0x4000
#define RTW88_CHECK_SUM_OK	0x0050
#define RTW88_FW_READY		0xc078
#define RTW88_REG_DDMA_SA	0x1200
#define RTW88_REG_DDMA_DA	0x1204
#define RTW88_REG_DDMA_CTRL	0x1208
#define RTW88_DDMA_OWN		UINT32_C(0x80000000)
#define RTW88_DDMA_CHKSUM_EN	UINT32_C(0x20000000)
#define RTW88_DDMA_CHKSUM_CONT	UINT32_C(0x01000000)
#define RTW88_DDMA_RESET_CK	UINT32_C(0x02000000)
#define RTW88_DDMA_CHKSUM_STS	UINT32_C(0x08000000)
#define RTW88_DDMA_LEN_MASK	0x3ffff
#define RTW88_OCP_TXBUF		UINT32_C(0x18780000)
#define RTW88_OCP_DMEM		UINT32_C(0x00200000)
#define RTW88_IMEM_DW_OK	0x08
#define RTW88_IMEM_CHKSUM_OK	0x10
#define RTW88_DMEM_DW_OK	0x20
#define RTW88_DMEM_CHKSUM_OK	0x40
#define RTW88_REG_TXDMA_STATUS	0x0210
#define RTW88_PAGE_OVF		0x04
#define RTW88_REG_FW_DBG7	0x10fc
#define RTW88_TX_DESC		48
#define RTW88_FW_CHUNK		0x1000
#define RTW88_REG_HMETFR	0x01cc
#define RTW88_REG_HMEBOX0	0x01d0
#define RTW88_REG_HMEBOX0_EX	0x01f0
#define RTW88_RING_BCN		4
#define RTW88_QSEL_BEACON	16
#define RTW88_REG_SYS_FUNC_EN	0x0002
#define RTW88_REG_SYS_CLK	0x0008
#define RTW88_REG_RSV_CTRL	0x001c
#define RTW88_REG_CR		0x0100
#define RTW88_REG_TXDMA_PQ	0x010c
#define RTW88_REG_FIFOPAGE_2	0x0204
#define RTW88_REG_RQPN_CTRL2	0x022c
#define RTW88_REG_FIFOPAGE_1	0x0230
#define RTW88_REG_TXBD_DESA_BCN	0x0308
#define RTW88_REG_PCI_CTRL	0x0300
#define RTW88_REG_BCN_WORK	0x0383
#define RTW88_REG_FWHW_TXQ	0x0420
#define RTW88_REG_BCN_CTRL	0x0550
#define RTW88_REG_CPU_DMEM	0x1080
#define RTW88_REG_H2CQ_CSR	0x1330
#define RTW88_BCN_WORK_FLAG	0x10
#define RTW88_REG_HIMR0		0x00b0
#define RTW88_REG_HISR0		0x00b4
#define RTW88_REG_HIMR1		0x00b8
#define RTW88_REG_HISR1		0x00bc
#define RTW88_REG_HIMR3		0x10b8
#define RTW88_REG_HISR3		0x10bc
/* pci.c rtw_pci_init irq_mask[0], [1], [3]. */
#define RTW88_IMR0		0x44fd	/* TX DOKs, ROK, BCNDMAINT_E, C2HCMD */
#define RTW88_IMR1		0x0200	/* TXFOVW */
#define RTW88_IMR3		0x10000	/* H2CDOK */
#define RTW88_IMR_ROK		0x01
#define RTW88_REG_RXBD_IDX	0x03b4
#define RTW88_REG_RWPTR_CLR	0x039c
#define RTW88_H2CQ_CLR_IDX	UINT32_C(0x00010100)
#define RTW88_PCI_DMA_RESET	UINT32_C(0x00108000)	/* TRXDMA reset, RX tag */
#define RTW88_RING_RX		8
#define RTW88_RX_TAG_MAX	8192
#define RTW88_MIN_FRAME		10	/* 802.11 ACK */
#define RTW88_REG_RCR		0x0608
/* main.c hal.rcr: APP_FCS/MIC/ICV/PHYSTS, PKTCTL_DLEN, HTC_LOC, AB/AM/APM. */
#define RTW88_RCR		UINT32_C(0xf010400e)
#define RTW88_BEACON_IES	36	/* header 24, timestamp, interval, caps */
#define RTW88_IMR_MGNTDOK	0x40
#define RTW88_IMR_BEDOK		0x10
#define RTW88_IMR_BKDOK		0x20
#define RTW88_IMR_VIDOK		0x08
#define RTW88_IMR_VODOK		0x04
#define RTW88_QSEL_MGMT		18
#define RTW88_RATEID_G		7
#define RTW88_RATEID_B_20M	8
/* rtlwifi REG_LBMODE (CR + 3): normal 0, PHY 1, MAC delay 3, DMA 7. */
#define RTW88_LBMODE_IMMEDIATE	0x0b

/* TX queues in use: ring index, TXBD_NUM, TXBD_IDX, completion bit. */
static const struct {
	unsigned ring;
	uint32_t num;
	uint32_t idx;
	uint32_t done;
} rtw88_txq_hw[RTW88_NTXQ] = {
	[RTW88_TXQ_MGMT] = { 5, 0x0380, 0x03b0, RTW88_IMR_MGNTDOK },
	[RTW88_TXQ_BE] = { 1, 0x0388, 0x03a8, RTW88_IMR_BEDOK },
	[RTW88_TXQ_BK] = { 0, 0x038a, 0x03ac, RTW88_IMR_BKDOK },
	[RTW88_TXQ_VI] = { 2, 0x0386, 0x03a4, RTW88_IMR_VIDOK },
	[RTW88_TXQ_VO] = { 3, 0x0384, 0x03a0, RTW88_IMR_VODOK },
};
#define RTW88_BD_OWN		0x8000
#define RTW88_BCN_VALID		0x8000

static int
rtw88_poll(struct rtw88_softc *sc, uint32_t gen, unsigned budget_us,
    int (*ready)(struct rtw88_softc *, void *), void *arg)
{
	unsigned waited = 0;
	int error;

	while (waited <= budget_us) {
		if (sc->sc_stop_requested || sc->sc_generation != gen)
			return EINTR;
		error = ready(sc, arg);
		if (error < 0)
			return -error;
		if (error == 0)
			return 0;
		sc->sc_ops->delay_us(sc->sc_bus, RTW88_POLL_STEP_US);
		waited += RTW88_POLL_STEP_US;
	}
	return ETIMEDOUT;
}

struct rtw88_poll32 {
	uint32_t reg;
	uint32_t mask;
	uint32_t value;
};

static int
rtw88_ready32(struct rtw88_softc *sc, void *arg)
{
	struct rtw88_poll32 *p = arg;
	uint32_t v;
	int error;

	error = sc->sc_ops->read_4(sc->sc_bus, p->reg, &v);
	if (error != 0)
		return -error;
	return ((v & p->mask) == p->value) ? 0 : 1;
}

static int
rtw88_ready8(struct rtw88_softc *sc, void *arg)
{
	struct rtw88_poll32 *p = arg;
	uint8_t v;
	int error;

	error = sc->sc_ops->read_1(sc->sc_bus, p->reg, &v);
	if (error != 0)
		return -error;
	return ((v & (uint8_t)p->mask) == (uint8_t)p->value) ? 0 : 1;
}

static void
rtw88_put16(uint8_t *p, uint16_t v)
{

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
rtw88_put32(uint8_t *p, uint32_t v)
{

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static int
rtw88_rmw8(struct rtw88_softc *sc, uint32_t addr, uint8_t clear, uint8_t set)
{
	uint8_t v;
	int error;

	error = sc->sc_ops->read_1(sc->sc_bus, addr, &v);
	if (error != 0)
		return error;
	v = (uint8_t)((v & ~clear) | set);
	return sc->sc_ops->write_1(sc->sc_bus, addr, v);
}

/*
 * The chip DDMA source is its internal TX buffer, not the host mapping.
 * A beacon-queue descriptor must copy the chunk there before DDMA runs.
 */
static int
rtw88_post_fw_chunk(struct rtw88_softc *sc, uint32_t gen, size_t len)
{
	struct rtw88_tx_info tx;
	struct rtw88_dma *bcn = &sc->sc_ring[RTW88_RING_BCN];
	struct rtw88_poll32 valid;
	uint8_t *bd;
	uint32_t total, psb, dma;
	int error;

	if (bcn->vaddr == NULL || bcn->size < RTW8822C_TX_BUFFER_DESC_SIZE)
		return ENXIO;
	if (sc->sc_fw_dma.addr > UINT32_MAX || bcn->addr > UINT32_MAX)
		return EOVERFLOW;
	memset(&tx, 0, sizeof(tx));
	tx.packet_size = (uint32_t)len;
	tx.queue = RTW88_QSEL_BEACON;
	tx.rate_id = 8;
	tx.flags = RTW88_TX_DIS_QSELSEQ | RTW88_TX_USE_RATE |
	    RTW88_TX_DIS_RATE_FB | RTW88_TX_HWSEQ;
	error = rtw88_tx_encode(sc->sc_fw_dma.vaddr, sc->sc_fw_dma.size, &tx);
	if (error != 0)
		return error;
	bd = bcn->vaddr;
	memset(bd, 0, RTW8822C_TX_BUFFER_DESC_SIZE);
	total = (uint32_t)len + RTW88_TX_DESC;
	psb = (total - 1) / 128 + 1;
	psb |= RTW88_BD_OWN;
	dma = (uint32_t)sc->sc_fw_dma.addr;
	rtw88_put16(bd, RTW88_TX_DESC);
	rtw88_put16(bd + 2, (uint16_t)psb);
	rtw88_put32(bd + 4, dma);
	rtw88_put16(bd + 8, (uint16_t)len);
	rtw88_put32(bd + 12, dma + RTW88_TX_DESC);
	sc->sc_ops->dma_sync(sc->sc_bus, &sc->sc_fw_dma, BUS_DMASYNC_PREWRITE);
	sc->sc_ops->dma_sync(sc->sc_bus, bcn, BUS_DMASYNC_PREWRITE);
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_TXBD_DESA_BCN,
	    (uint32_t)bcn->addr);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_FIFOPAGE_2,
	    RTW88_BCN_VALID);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_CR + 1, 0, 0x01);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_BCN_CTRL, 0x08, 0x10);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_FWHW_TXQ + 2, 0x40, 0);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_BCN_WORK, 0, RTW88_BCN_WORK_FLAG);
	if (error != 0)
		return error;
	/*
	 * The beacon queue has no completion write-back: the host OWN bit
	 * stays set. The chip reports the copy by setting BCN_VALID.
	 */
	valid.reg = RTW88_REG_FIFOPAGE_2 + 1;
	valid.mask = RTW88_BCN_VALID >> 8;
	valid.value = RTW88_BCN_VALID >> 8;
	error = rtw88_poll(sc, gen, RTW88_COMMAND_US, rtw88_ready8, &valid);
	if (error != 0) {
		uint32_t desa = 0, cr = 0;
		uint16_t page = 0;

		(void)sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_TXBD_DESA_BCN,
		    &desa);
		(void)sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_CR, &cr);
		(void)sc->sc_ops->read_2(sc->sc_bus, RTW88_REG_FIFOPAGE_2,
		    &page);
		printf("rtwce: firmware beacon queue timed out "
		    "bd %#lx data %#lx desa %#x cr %#x page %#x own %#x\n",
		    (unsigned long)sc->sc_ring[RTW88_RING_BCN].addr,
		    (unsigned long)sc->sc_fw_dma.addr, desa, cr, page,
		    (unsigned)bd[2] | (unsigned)bd[3] << 8);
		return error;
	}
	return 0;
}

static int rtw88_hci_setup(struct rtw88_softc *);
static int rtw88_hci_start(struct rtw88_softc *);

static int
rtw88_fw_prepare(struct rtw88_softc *sc)
{
	uint32_t rqpn, dmem;
	int error;

	error = rtw88_rmw8(sc, RTW88_REG_SYS_FUNC_EN + 1, 0x04, 0);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_RSV_CTRL + 1, 0x01, 0);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_TXDMA_PQ + 1, 0xc0);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_CR, 0x05);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_H2CQ_CSR,
	    UINT32_C(0x80000000));
	if (error != 0)
		return error;
	error = sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_FIFOPAGE_1, 0x0200);
	if (error != 0)
		return error;
	error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_RQPN_CTRL2, &rqpn);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_RQPN_CTRL2,
	    rqpn | UINT32_C(0x80000000));
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_BCN_CTRL, 0x08, 0x10);
	if (error != 0)
		return error;
	error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_CPU_DMEM, &dmem);
	if (error != 0)
		return error;
	dmem |= UINT32_C(0x00010100);
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_CPU_DMEM, dmem);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_CPU_DMEM + 2, 0x01, 0);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_SYS_CLK + 1, 0x40, 0);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_CPU_DMEM + 2, 0, 0x01);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_SYS_CLK + 1, 0, 0x40);
	if (error != 0)
		return error;
	return rtw88_hci_setup(sc);
}

static int
rtw88_write_ring(struct rtw88_softc *sc, uint32_t desa, uint32_t num,
    int with_num, unsigned index, unsigned desc)
{
	struct rtw88_dma *ring = &sc->sc_ring[index];
	int error;

	if (ring->vaddr == NULL || ring->addr > UINT32_MAX ||
	    desc == 0 || ring->size < desc || (ring->size % desc) != 0)
		return EINVAL;
	error = sc->sc_ops->write_4(sc->sc_bus, desa, (uint32_t)ring->addr);
	if (error != 0 || !with_num)
		return error;
	return sc->sc_ops->write_2(sc->sc_bus, num,
	    (uint16_t)((ring->size / desc) & 0xfff));
}

/* Point every RX descriptor at its buffer, owned by the chip. */
static void
rtw88_rx_arm(struct rtw88_softc *sc, unsigned idx)
{
	uint8_t *bd = (uint8_t *)sc->sc_ring[RTW88_RING_RX].vaddr +
	    RTW8822C_RX_BUFFER_DESC_SIZE * idx;

	rtw88_put16(bd, RTW88_RX_BUF_SIZE);
	rtw88_put16(bd + 2, 0);
	rtw88_put32(bd + 4, (uint32_t)sc->sc_rxbuf[idx].addr);
	sc->sc_ops->dma_sync(sc->sc_bus, &sc->sc_rxbuf[idx],
	    BUS_DMASYNC_PREREAD);
}

/*
 * pci.c rtw_pci_setup: publish rings, clear indices, reset TRX DMA.
 * Used before firmware download and again after it, as the reference.
 */
static int
rtw88_hci_setup(struct rtw88_softc *sc)
{
	uint32_t ctrl, csr;
	uint8_t pci;
	unsigned i;
	int error;

	error = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_PCI_CTRL + 3, &pci);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_PCI_CTRL + 3,
	    (uint8_t)(pci | 0xf7));
	if (error != 0)
		return error;
	/* Beacon, H2C, BK, BE, VO, VI, management, high, then MPDU RX. */
	error = rtw88_write_ring(sc, RTW88_REG_TXBD_DESA_BCN, 0, 0,
	    RTW88_RING_BCN, 16);
	if (error == 0)
		error = rtw88_write_ring(sc, 0x1320, 0x1328, 1, 7, 16);
	if (error == 0)
		error = rtw88_write_ring(sc, 0x0330, 0x038a, 1, 0, 16);
	if (error == 0)
		error = rtw88_write_ring(sc, 0x0328, 0x0388, 1, 1, 16);
	if (error == 0)
		error = rtw88_write_ring(sc, 0x0318, 0x0384, 1, 3, 16);
	if (error == 0)
		error = rtw88_write_ring(sc, 0x0320, 0x0386, 1, 2, 16);
	if (error == 0)
		error = rtw88_write_ring(sc, 0x0310, 0x0380, 1, 5, 16);
	if (error == 0)
		error = rtw88_write_ring(sc, 0x0340, 0x038c, 1, 6, 16);
	for (i = 0; i < RTW88_NTXQ && error == 0; i++) {
		struct rtw88_txq *txq = &sc->sc_txq[i];

		if (txq->nbuf != RTW88_TX_SLOTS)
			return EINVAL;
		memset(sc->sc_ring[rtw88_txq_hw[i].ring].vaddr, 0,
		    RTW88_TX_SLOTS * RTW8822C_TX_BUFFER_DESC_SIZE);
		txq->wp = txq->rp = txq->queued = 0;
		error = sc->sc_ops->write_2(sc->sc_bus, rtw88_txq_hw[i].num,
		    RTW88_TX_SLOTS);
	}
	if (error != 0)
		return error;
	if (sc->sc_nrxbuf == 0 ||
	    sc->sc_ring[RTW88_RING_RX].size <
	    sc->sc_nrxbuf * RTW8822C_RX_BUFFER_DESC_SIZE)
		return EINVAL;
	for (i = 0; i < sc->sc_nrxbuf; i++)
		rtw88_rx_arm(sc, i);
	sc->sc_ops->dma_sync(sc->sc_bus, &sc->sc_ring[RTW88_RING_RX],
	    BUS_DMASYNC_PREWRITE);
	sc->sc_rx_rp = 0;
	sc->sc_rx_tag = 0;
	error = sc->sc_ops->write_2(sc->sc_bus, 0x0382,
	    (uint16_t)(sc->sc_nrxbuf & 0xfff));
	if (error == 0)
		error = sc->sc_ops->write_4(sc->sc_bus, 0x0338,
		    (uint32_t)sc->sc_ring[RTW88_RING_RX].addr);
	if (error == 0)
		error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_RWPTR_CLR,
		    UINT32_C(0xffffffff));
	if (error == 0)
		error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_H2CQ_CSR, &csr);
	if (error == 0)
		error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_H2CQ_CSR,
		    csr | RTW88_H2CQ_CLR_IDX);
	if (error == 0)
		error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_PCI_CTRL, &ctrl);
	if (error != 0)
		return error;
	return sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_PCI_CTRL,
	    ctrl | RTW88_PCI_DMA_RESET);
}

static int
rtw88_iddma_chunk(struct rtw88_softc *sc, uint32_t gen, const uint8_t *data,
    size_t len, uint32_t dst, int first)
{
	struct rtw88_poll32 own;
	uint32_t ctrl;
	int error;

	if (sc->sc_fw_dma.vaddr == NULL ||
	    len + RTW88_TX_DESC > sc->sc_fw_dma.size)
		return EOVERFLOW;
	memcpy((uint8_t *)sc->sc_fw_dma.vaddr + RTW88_TX_DESC, data, len);
	error = rtw88_post_fw_chunk(sc, gen, len);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_DDMA_SA,
	    RTW88_OCP_TXBUF + RTW88_TX_DESC);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_DDMA_DA, dst);
	if (error != 0)
		return error;
	ctrl = RTW88_DDMA_OWN | RTW88_DDMA_CHKSUM_EN |
	    (len & RTW88_DDMA_LEN_MASK);
	if (!first)
		ctrl |= RTW88_DDMA_CHKSUM_CONT;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_DDMA_CTRL, ctrl);
	if (error != 0)
		return error;
	own.reg = RTW88_REG_DDMA_CTRL;
	own.mask = RTW88_DDMA_OWN;
	own.value = 0;
	error = rtw88_poll(sc, gen, RTW88_COMMAND_US, rtw88_ready32, &own);
	if (error != 0)
		printf("rtwce: firmware DDMA timed out\n");
	return error;
}

/*
 * The driver, not the firmware, reports each section to the WCPU ROM:
 * DW_OK, plus CHKSUM_OK when DDMA saw a good checksum. IMEM and EMEM
 * sit below the DMEM base and share the IMEM bits.
 */
static int
rtw88_section_checksum(struct rtw88_softc *sc, uint32_t addr)
{
	uint32_t ddma;
	uint8_t fw, dw, ok;
	int error;

	error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_DDMA_CTRL, &ddma);
	if (error != 0)
		return error;
	error = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_MCUFW_CTRL, &fw);
	if (error != 0)
		return error;
	if (addr < RTW88_OCP_DMEM) {
		dw = RTW88_IMEM_DW_OK;
		ok = RTW88_IMEM_CHKSUM_OK;
	} else {
		dw = RTW88_DMEM_DW_OK;
		ok = RTW88_DMEM_CHKSUM_OK;
	}
	if ((ddma & RTW88_DDMA_CHKSUM_STS) != 0) {
		(void)sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_MCUFW_CTRL,
		    (uint8_t)((fw | dw) & ~ok));
		printf("rtwce: firmware checksum failed at %#x\n", addr);
		return EIO;
	}
	return sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_MCUFW_CTRL,
	    (uint8_t)(fw | dw | ok));
}

static int
rtw88_download_section(struct rtw88_softc *sc, uint32_t gen,
    const uint8_t *fw, const struct rtw88_fw_section *section)
{
	size_t off = 0;
	int first = 1, error;
	uint32_t ctrl;

	error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_DDMA_CTRL, &ctrl);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_DDMA_CTRL,
	    ctrl | RTW88_DDMA_RESET_CK);
	if (error != 0)
		return error;
	while (off < section->size) {
		size_t chunk = section->size - off;

		if (chunk > RTW88_FW_CHUNK)
			chunk = RTW88_FW_CHUNK;
		error = rtw88_iddma_chunk(sc, gen, fw + section->offset + off,
		    chunk, section->load_address + (uint32_t)off, first);
		if (error != 0)
			return error;
		first = 0;
		off += chunk;
	}
	return rtw88_section_checksum(sc, section->load_address);
}

static int
rtw88_fw_download(struct rtw88_softc *sc, uint32_t gen, const uint8_t *fw,
    const struct rtw88_fw_info *info)
{
	struct rtw88_poll32 ready;
	uint16_t ctrl;
	int error;

	error = sc->sc_ops->read_2(sc->sc_bus, RTW88_REG_MCUFW_CTRL, &ctrl);
	if (error != 0)
		return error;
	ctrl = (uint16_t)((ctrl & 0x3800) | RTW88_MCUFWDL_EN);
	error = sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_MCUFW_CTRL, ctrl);
	if (error != 0)
		return error;
	error = rtw88_fw_prepare(sc);
	if (error != 0)
		return error;
	error = rtw88_download_section(sc, gen, fw, &info->dmem);
	if (error != 0)
		return error;
	error = rtw88_download_section(sc, gen, fw, &info->imem);
	if (error != 0)
		return error;
	if (info->emem.size != 0) {
		error = rtw88_download_section(sc, gen, fw, &info->emem);
		if (error != 0)
			return error;
	}
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_TXDMA_STATUS,
	    RTW88_PAGE_OVF);
	if (error != 0)
		return error;
	error = sc->sc_ops->read_2(sc->sc_bus, RTW88_REG_MCUFW_CTRL, &ctrl);
	if (error != 0)
		return error;
	/* Reference end flow: no FW_DW_RDY without both checksums. */
	if ((ctrl & RTW88_CHECK_SUM_OK) != RTW88_CHECK_SUM_OK)
		return EIO;
	ctrl = (uint16_t)((ctrl | RTW88_FW_DW_RDY) & ~RTW88_MCUFWDL_EN);
	error = sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_MCUFW_CTRL, ctrl);
	if (error != 0)
		return error;
	/* FW_INIT_RDY joins the driver-set bits once the image runs. */
	ready.reg = RTW88_REG_MCUFW_CTRL;
	ready.mask = RTW88_FW_READY;
	ready.value = RTW88_FW_READY;
	error = rtw88_rmw8(sc, RTW88_REG_RSV_CTRL + 1, 0, 0x01);
	if (error != 0)
		return error;
	error = rtw88_rmw8(sc, RTW88_REG_SYS_FUNC_EN + 1, 0, 0x04);
	if (error != 0)
		return error;
	error = rtw88_poll(sc, gen, RTW88_BOOTSTRAP_US, rtw88_ready32, &ready);
	if (error != 0) {
		uint32_t mcu = 0, dbg7 = 0;

		(void)sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_MCUFW_CTRL, &mcu);
		(void)sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_FW_DBG7, &dbg7);
		printf("rtwce: firmware ready timed out mcufw %#x dbg7 %#x\n",
		    mcu, dbg7);
	}
	return error;
}

static void
rtw88_release_fw(struct rtw88_softc *sc)
{

	if (sc->sc_ops->firmware_release != NULL)
		sc->sc_ops->firmware_release(sc->sc_bus);
}

int
rtw88_stop(struct rtw88_softc *sc)
{
	int error;

	sc->sc_stop_requested = 1;
	sc->sc_generation++;
	sc->sc_packet_h2c.active = 0;
	if (sc->sc_running) {
		/* No new handler work, masks off, then wait out a running one. */
		sc->sc_running = 0;
		(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR0, 0);
		(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR1, 0);
		(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR3, 0);
		if (sc->sc_ops->intr_barrier != NULL)
			sc->sc_ops->intr_barrier(sc->sc_bus);
		for (unsigned q = 0; q < RTW88_NTXQ; q++) {
			sc->sc_stats.tx_dropped += sc->sc_txq[q].queued;
			sc->sc_txq[q].queued = 0;
		}
	}
	sc->sc_started = 0;
	sc->sc_rf_enabled = 0;
	sc->sc_bootstrapping = 0;
	if (sc->sc_power_dirty || sc->sc_powered) {
		error = rtw8822c_power(sc, 0);
		sc->sc_power_error = error;
		if (error == 0) {
			sc->sc_powered = 0;
			sc->sc_power_dirty = 0;
		} else {
			sc->sc_power_dirty = 1;
			printf("rtwce: power off failed: %d; restart blocked\n", error);
		}
	}
	if (sc->sc_dma_master) {
		static const uint32_t desa[] = {
			0x0330, 0x0328, 0x0320, 0x0318, RTW88_REG_TXBD_DESA_BCN,
			0x0310, 0x0340, 0x1320, 0x0338
		};
		unsigned i;

		for (i = 0; i < __arraycount(desa); i++)
			(void)sc->sc_ops->write_4(sc->sc_bus, desa[i], 0);
		error = sc->sc_ops->bus_master == NULL ? ENODEV :
		    sc->sc_ops->bus_master(sc->sc_bus, 0);
		sc->sc_dma_error = error;
		if (error == 0)
			sc->sc_dma_master = 0;
		else
			printf("rtwce: bus-master disable failed: %d; DMA retained\n", error);
	}
	if (sc->sc_fw_dma_owned && !sc->sc_dma_master) {
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_fw_dma);
		sc->sc_fw_dma_owned = 0;
	}
	return sc->sc_dma_error != 0 ? sc->sc_dma_error : sc->sc_power_error;
}

int
rtw88_start(struct rtw88_softc *sc)
{
	const uint8_t *fw = NULL;
	const char *stage = "identity";
	size_t fw_len = 0;
	struct rtw88_fw_info info;
	struct rtw8822c_efuse efuse;
	uint32_t gen;
	int error;

	if (!sc->sc_resources_ready)
		return ENXIO;
	if (sc->sc_started)
		return EBUSY;
	if (sc->sc_power_dirty || sc->sc_power_error || sc->sc_dma_master ||
	    sc->sc_dma_error) {
		printf("rtwce: startup blocked by incomplete shutdown: power %d DMA %d\n",
		    sc->sc_power_error, sc->sc_dma_error);
		return EBUSY;
	}
	if (sc->sc_ops->firmware_load == NULL || sc->sc_ops->bus_master == NULL)
		return ENODEV;
	error = sc->sc_ops->firmware_load(sc->sc_bus, RTW88_FW_DRV,
	    RTW88_FW_IMG, &fw, &fw_len);
	if (error != 0)
		return error;
	error = rtw88_fw_parse(fw, fw_len, &info);
	if (error != 0) {
		rtw88_release_fw(sc);
		return error;
	}
	sc->sc_generation++;
	gen = sc->sc_generation;
	sc->sc_stop_requested = 0;
	sc->sc_bootstrapping = 1;
	error = rtw8822c_identify(sc);
	if (error != 0)
		goto fail;
	stage = "firmware DMA allocation";
	error = sc->sc_ops->dma_alloc(sc->sc_bus, &sc->sc_fw_dma,
	    RTW88_FW_PAGE_SIZE);
	if (error != 0)
		goto fail;
	sc->sc_fw_dma_owned = 1;
	error = rtw88_dma_validate(sc->sc_fw_dma.addr, sc->sc_fw_dma.size,
	    RTW8822C_DMA_ADDR_BITS);
	if (error != 0)
		goto fail;
	stage = "bus mastering";
	/* An enable method may fail after hardware has accepted the write. */
	sc->sc_dma_master = 1;
	error = sc->sc_ops->bus_master(sc->sc_bus, 1);
	if (error != 0)
		goto fail;
	stage = "MAC power preinit";
	/* Partial pre/table/post writes also require a complete off attempt. */
	sc->sc_power_dirty = 1;
	error = rtw8822c_power_pre(sc);
	if (error != 0)
		goto fail;
	stage = "MAC power";
	error = rtw8822c_power(sc, 1);
	if (error != 0) {
		printf("rtwce: power on failed: %d\n", error);
		goto fail;
	}
	sc->sc_powered = 1;
	stage = "MAC power postinit";
	error = rtw8822c_power_post(sc);
	if (error != 0)
		goto fail;
	stage = "firmware download";
	error = rtw88_fw_download(sc, gen, fw, &info);
	if (error != 0)
		goto fail;
	sc->sc_h2c_box = 0;
	/* Only a confirmed new firmware image starts a new packet sequence. */
	memset(&sc->sc_packet_h2c, 0, sizeof(sc->sc_packet_h2c));
	sc->sc_packet_h2c.active = 1;
	sc->sc_packet_h2c.generation = gen;
	/* Reference rtw_power_on: reset rings, MAC, PHY, then HCI start. */
	stage = "PCI rings";
	error = rtw88_hci_setup(sc);
	if (error != 0)
		goto fail;
	stage = "MAC setup";
	error = rtw8822c_mac_init(sc);
	if (error != 0) {
		printf("rtwce: MAC init failed: %d\n", error);
		goto fail;
	}
	/* rtw8822c_phy_set_param: BB/RF on, tables, paths, EDCCA, BF. */
	stage = "PHY preinit";
	error = rtw8822c_phy_pre(sc);
	if (error != 0)
		goto fail;
	stage = "PHY tables";
	error = rtw8822c_load_tables(sc);
	if (error != 0)
		goto fail;
	stage = "PHY postinit";
	error = rtw8822c_phy_post(sc);
	if (error != 0)
		goto fail;
	/* rtw_core_start: RCR = hal.rcr, accepting any BSSID's beacons. */
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_RCR, RTW88_RCR);
	if (error != 0)
		goto fail;
	stage = "channel setup";
	error = rtw8822c_set_channel(sc, RTW88_DEFAULT_CHANNEL);
	if (error != 0)
		goto fail;
	stage = "TX power";
	error = rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &efuse);
	if (error == 0)
		error = rtw8822c_set_tx_power(sc, &efuse, RTW88_DEFAULT_CHANNEL, 12);
	if (error != 0)
		goto fail;
	sc->sc_channel = RTW88_DEFAULT_CHANNEL;
	if (sc->sc_stop_requested || sc->sc_generation != gen) {
		error = EINTR;
		goto fail;
	}
	stage = "HCI start";
	error = rtw88_hci_start(sc);
	if (error != 0)
		goto fail;
	stage = "packet H2C";
	error = rtw88_fw_init(sc);
	if (error != 0)
		goto fail;
	stage = "coex power-on";
	error = rtw8822c_coex_power_on(sc);
	if (error != 0)
		goto fail;
	stage = "coex init";
	error = rtw8822c_coex_init(sc);
	if (error != 0)
		goto fail;
	if (sc->sc_stop_requested || sc->sc_generation != gen) {
		error = EINTR;
		goto fail;
	}
	sc->sc_rf_enabled = 1;
	sc->sc_edca_error = 0;
	sc->sc_started = 1;
	sc->sc_bootstrapping = 0;
	rtw88_release_fw(sc);
	return 0;
fail:
	printf("rtwce: startup %s failed: %d\n", stage, error);
	rtw88_stop(sc);
	rtw88_release_fw(sc);
	return error;
}

int
rtw88_fw_command(struct rtw88_softc *sc, uint8_t id, const void *payload,
    size_t size)
{
	struct rtw88_poll32 box;
	const uint8_t *p = payload;
	uint32_t gen, w0, w1, ex_reg, box_reg;
	int error;

	if (!sc->sc_started || !sc->sc_rf_enabled)
		return ENXIO;
	if (size > 0 && payload == NULL)
		return EINVAL;
	if (size > RTW88_FW_CMD_MAX)
		return EMSGSIZE;
	gen = sc->sc_generation;
	box.reg = RTW88_REG_HMETFR;
	box.mask = (uint32_t)1 << (sc->sc_h2c_box & 3);
	box.value = 0;
	error = rtw88_poll(sc, gen, RTW88_COMMAND_US, rtw88_ready8, &box);
	if (error == ETIMEDOUT) {
		uint32_t mcu = 0, imr = 0, isr = 0, boxes = 0;

		(void)sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_MCUFW_CTRL, &mcu);
		(void)sc->sc_ops->read_4(sc->sc_bus, 0x0130, &imr);
		(void)sc->sc_ops->read_4(sc->sc_bus, 0x0134, &isr);
		(void)sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_HMETFR, &boxes);
		printf("rtwce: mailbox %u command %#x timed out "
		    "mcufw %#x fwimr %#x fwisr %#x boxes %#x\n",
		    (unsigned)(sc->sc_h2c_box & 3), id, mcu, imr, isr, boxes);
	}
	if (error != 0)
		return error;
	w0 = id;
	w1 = 0;
	if (size > 0)
		w0 |= (uint32_t)p[0] << 8;
	if (size > 1)
		w0 |= (uint32_t)p[1] << 16;
	if (size > 2)
		w0 |= (uint32_t)p[2] << 24;
	if (size > 3)
		w1 |= p[3];
	if (size > 4)
		w1 |= (uint32_t)p[4] << 8;
	if (size > 5)
		w1 |= (uint32_t)p[5] << 16;
	if (size > 6)
		w1 |= (uint32_t)p[6] << 24;
	box_reg = RTW88_REG_HMEBOX0 + 4 * (sc->sc_h2c_box & 3);
	ex_reg = RTW88_REG_HMEBOX0_EX + 4 * (sc->sc_h2c_box & 3);
	error = sc->sc_ops->write_4(sc->sc_bus, ex_reg, w1);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_4(sc->sc_bus, box_reg, w0);
	if (error != 0)
		return error;
	sc->sc_h2c_box = (sc->sc_h2c_box + 1) & 3;
	if (sc->sc_generation != gen)
		return EINTR;
	return 0;
}

/* pci.c rtw_pci_start: unmask the reference interrupt causes. */
static int
rtw88_hci_start(struct rtw88_softc *sc)
{
	int error;

	if (sc->sc_ops->intr_barrier == NULL)
		return ENODEV;
	sc->sc_running = 1;
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR0, RTW88_IMR0);
	if (error == 0)
		error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR1,
		    RTW88_IMR1);
	if (error == 0)
		error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR3,
		    RTW88_IMR3);
	return error;
}

/* Beacon evidence: BSSID and SSID, the SSID only when its IE fits. */
static void
rtw88_rx_beacon(struct rtw88_softc *sc, const uint8_t *p, size_t len)
{
	struct rtw88_stats *st = &sc->sc_stats;
	size_t n;

	st->rx_beacons++;
	memcpy(st->last_bssid, p + 16, sizeof(st->last_bssid));
	st->last_ssid_len = 0;
	st->last_ssid[0] = '\0';
	if (len < RTW88_BEACON_IES + 2 || p[RTW88_BEACON_IES] != 0)
		return;
	n = p[RTW88_BEACON_IES + 1];
	if (n > sizeof(st->last_ssid) - 1 || RTW88_BEACON_IES + 2 + n > len)
		return;
	memcpy(st->last_ssid, p + RTW88_BEACON_IES + 2, n);
	st->last_ssid[n] = '\0';
	st->last_ssid_len = (uint8_t)n;
}

/* One received buffer: classify, count, and keep every error visible. */
static void
rtw88_rx_one(struct rtw88_softc *sc, unsigned idx)
{
	struct rtw88_dma *buf = &sc->sc_rxbuf[idx];
	struct rtw88_rx_info rx;
	const uint8_t *p;

	sc->sc_ops->dma_sync(sc->sc_bus, buf, BUS_DMASYNC_POSTREAD);
	if (rtw88_rx_decode(buf->vaddr, RTW88_RX_BUF_SIZE, &rx) != 0) {
		sc->sc_stats.rx_errors++;
		return;
	}
	p = (const uint8_t *)buf->vaddr + rx.payload_offset;
	if ((rx.flags & RTW88_RX_C2H) != 0) {
		if (rx.packet_size < 2) {
			sc->sc_stats.rx_errors++;
			return;
		}
		sc->sc_stats.rx_c2h++;
		sc->sc_stats.last_c2h_id = p[0];
		sc->sc_stats.last_c2h_seq = p[1];
		return;
	}
	if ((rx.flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR)) != 0) {
		sc->sc_stats.rx_errors++;
		return;
	}
	if (rx.packet_size < RTW88_MIN_FRAME) {
		sc->sc_stats.rx_short++;
		return;
	}
	/* The diagnostic counters include valid control frames. */
	sc->sc_stats.rx_frames++;
	if (p[0] == 0x80 && rx.packet_size >= RTW88_BEACON_IES)
		rtw88_rx_beacon(sc, p, rx.packet_size);
	/* RCR.APP_FCS is set. Never expose the four FCS bytes to net80211. */
	if (sc->sc_rx_frame != NULL && rx.packet_size >= 24 + 4) {
		rx.packet_size -= 4;
		sc->sc_rx_frame(sc->sc_net_cookie, p, rx.packet_size, &rx);
	}
}

/* pci.c rtw_pci_rx_napi, bounded by RTW88_RX_LIMIT per interrupt. */
static void
rtw88_rx(struct rtw88_softc *sc)
{
	struct rtw88_dma *ring = &sc->sc_ring[RTW88_RING_RX];
	unsigned hw, rp = sc->sc_rx_rp, count;
	uint32_t idx;
	uint16_t tag;
	uint8_t *bd;

	if (sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_RXBD_IDX, &idx) != 0)
		return;
	hw = (idx >> 16) & 0xfff;
	if (hw >= sc->sc_nrxbuf) {
		sc->sc_stats.rx_dma_errors++;
		return;
	}
	count = hw >= rp ? hw - rp : sc->sc_nrxbuf - rp + hw;
	if (count > RTW88_RX_LIMIT)
		count = RTW88_RX_LIMIT;
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_POSTREAD);
	while (count-- > 0) {
		bd = (uint8_t *)ring->vaddr + RTW8822C_RX_BUFFER_DESC_SIZE * rp;
		tag = (uint16_t)(bd[2] | bd[3] << 8);
		if (tag != sc->sc_rx_tag)
			sc->sc_stats.rx_tag_errors++;
		else
			rtw88_rx_one(sc, rp);
		sc->sc_rx_tag = (sc->sc_rx_tag + 1) % RTW88_RX_TAG_MAX;
		rtw88_rx_arm(sc, rp);
		rp = (rp + 1) % sc->sc_nrxbuf;
	}
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
	sc->sc_rx_rp = rp;
	(void)sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_RXBD_IDX, (uint16_t)rp);
}

unsigned
rtw88_tx_available(struct rtw88_softc *sc, unsigned q)
{
	const struct rtw88_txq *txq;

	if (q >= RTW88_NTXQ || !sc->sc_running || sc->sc_edca_error != 0)
		return 0;
	txq = &sc->sc_txq[q];
	return txq->rp > txq->wp ? txq->rp - txq->wp - 1 :
	    RTW88_TX_SLOTS - txq->wp + txq->rp - 1;
}

/*
 * Port0 fields and mailbox layouts are from pinned rtw88 main.c,
 * fw.c/fw.h, sec.c and reg.h. This host-managed legacy station does
 * not enable hardware crypto, firmware rate adaptation, LPS or offload.
 */
int
rtw88_station_config(struct rtw88_softc *sc, const uint8_t *bssid,
    unsigned aid, unsigned basic_rates, unsigned slot, int linked)
{
	struct rtw8822c_io io = { sc, 0 };
	uint8_t media[3] = { 0 }, port[3] = { 0 }, any = 0;
	unsigned i;

	if (linked != 0 && linked != 1)
		return EINVAL;
	if (basic_rates == 0 || (basic_rates & ~0xfffU) != 0 ||
	    (slot != 9 && slot != 20))
		return EINVAL;
	if (linked) {
		if (bssid == NULL || (bssid[0] & 1) != 0 ||
		    aid == 0 || aid > 2007)
			return EINVAL;
		for (i = 0; i < 6; i++)
			any |= bssid[i];
		if (any == 0)
			return EINVAL;
	} else if (aid != 0 || bssid != NULL) {
		return EINVAL;
	}
	if (sc->sc_edca_error != 0)
		return sc->sc_edca_error;
	if (!sc->sc_running || !sc->sc_started)
		return ENXIO;
	if (sc->sc_channel > 14 && (basic_rates & 0xf) != 0)
		return EINVAL;
	/* Keep data paused and port unlinked until all setup writes succeed. */
	rtw8822c_io_wr_mask(&io, 0x0100, 0x30000, 0);
	rtw8822c_io_set(&io, 0x0522, 1, 0x0f);
	rtw8822c_io_wr(&io, 0x06a4, 2, 0);
	for (i = 0; i < 6; i++) {
		rtw8822c_io_wr(&io, 0x0610 + i, 1, sc->sc_mac[i]);
		rtw8822c_io_wr(&io, 0x0618 + i, 1, linked ? bssid[i] : 0);
	}
	/* SECCFG.TXDEC/RXDEC are off: the stack owns CCMP/PN/MIC. */
	rtw8822c_io_clr(&io, 0x0680, 1, 0x0c);
	rtw8822c_io_wr_mask(&io, 0x06a8, 0x7ff, aid);
	rtw8822c_io_wr_mask(&io, 0x0440, 0xfff, basic_rates);
	rtw8822c_io_wr(&io, 0x051b, 1, slot);
	rtw8822c_io_set(&io, 0x0550, 1, 0x18);
	rtw8822c_io_clr(&io, 0x0608, 4, 0xc0);
	if (io.error != 0)
		return io.error;
	media[0] = linked ? 1 : 0;
	io.error = rtw88_fw_command(sc, 0x01, media, sizeof(media));
	if (linked && io.error == 0)
		io.error = rtw88_fw_command(sc, 0x2c, port, sizeof(port));
	if (linked) {
		rtw8822c_io_set(&io, 0x0608, 4, 0xc0);
		rtw8822c_io_clr(&io, 0x0550, 1, 0x10);
		rtw8822c_io_wr(&io, 0x06a4, 2, 0xffff);
		rtw8822c_io_wr_mask(&io, 0x0100, 0x30000, 2);
		rtw8822c_io_clr(&io, 0x0522, 1, 0x0f);
	}
	if (io.error == 0)
		sc->sc_basic_rates = basic_rates;
	return io.error;
}

/* EDCA register layout follows pinned BSD-licensed rtw88 mac80211.c. */
int
rtw88_edca_apply(struct rtw88_softc *sc,
    const struct rtw88_edca_params params[RTW88_WME_NAC], unsigned slot)
{
	static const uint32_t regs[RTW88_WME_NAC] = {
		0x0508, 0x050c, 0x0504, 0x0500
	};
	uint32_t words[RTW88_WME_NAC];
	uint8_t pause;
	unsigned i;
	int error;

	if (sc->sc_edca_error != 0)
		return sc->sc_edca_error;
	if (!sc->sc_resources_ready || !sc->sc_running || !sc->sc_started ||
	    sc->sc_stop_requested)
		return ENXIO;
	if (sc->sc_channel == 0 || (sc->sc_channel > 14 && slot != 9))
		return EINVAL;
	error = rtw88_edca_encode(params, slot,
	    sc->sc_channel > 14 ? 16 : 10, words);
	if (error != 0)
		return error;
	error = sc->sc_ops->read_1(sc->sc_bus, 0x0522, &pause);
	if (error == 0)
		error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, 0xff);
	for (i = 0; error == 0 && i < RTW88_WME_NAC; i++)
		error = sc->sc_ops->write_4(sc->sc_bus, regs[i], words[i]);
	/* Never unpause a partial configuration or retry uncertain writes. */
	if (error == 0)
		error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, pause);
	if (error != 0)
		sc->sc_edca_error = error;
	return error;
}

static int
rtw88_tx_submit(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, int fixed_rate)
{
	struct rtw88_tx_info tx;
	struct rtw88_wme_map map;
	struct rtw88_txq *txq;
	struct rtw88_dma *ring, *buf;
	uint8_t *bd;
	uint32_t addr;
	unsigned avail, rates, qsel, hdrlen;
	const uint8_t *bytes = frame;
	int error;

	if (q >= RTW88_NTXQ)
		return EINVAL;
	if (sc->sc_edca_error != 0)
		return sc->sc_edca_error;
	if (!sc->sc_resources_ready || !sc->sc_running || !sc->sc_started ||
	    sc->sc_stop_requested)
		return ENXIO;
	if (frame == NULL || len < 24 ||
	    len > RTW88_TX_BUF_SIZE - RTW8822C_TX_PACKET_DESC_SIZE)
		return EMSGSIZE;
	qsel = q == RTW88_TXQ_MGMT ? RTW88_QSEL_MGMT : 0;
	if ((bytes[0] & 0x8c) == 0x88) {
		/* HT Control is outside this legacy/QoS hardware prerequisite. */
		if ((bytes[1] & 0x80) != 0)
			return EOPNOTSUPP;
		hdrlen = (bytes[1] & 3) == 3 ? 32 : 26;
		if (len < hdrlen)
			return EMSGSIZE;
		if (q == RTW88_TXQ_MGMT)
			return EOPNOTSUPP;
		error = rtw88_wme_map(bytes[hdrlen - 2] & 15, &map);
		if (error != 0)
			return error;
		/* Queue ownership must match the actual QoS TID. */
		if (map.ring != rtw88_txq_hw[q].ring)
			return EOPNOTSUPP;
		qsel = map.qsel;
	} else if (q != RTW88_TXQ_MGMT && q != RTW88_TXQ_BE) {
		return EOPNOTSUPP;
	}
	txq = &sc->sc_txq[q];
	ring = &sc->sc_ring[rtw88_txq_hw[q].ring];
	if (txq->nbuf != RTW88_TX_SLOTS || ring->cookie == NULL ||
	    ring->vaddr == NULL ||
	    ring->size < RTW88_TX_SLOTS * RTW8822C_TX_BUFFER_DESC_SIZE)
		return ENXIO;
	if (txq->wp >= RTW88_TX_SLOTS || txq->rp >= RTW88_TX_SLOTS)
		return EPROTO;
	/* pci.h avail_desc: one slot stays empty to tell full from empty. */
	avail = rtw88_tx_available(sc, q);
	if (avail == 0) {
		sc->sc_stats.tx_full++;
		return ENOBUFS;
	}
	buf = &txq->buf[txq->wp];
	if (buf->cookie == NULL || buf->vaddr == NULL ||
	    buf->size < RTW8822C_TX_PACKET_DESC_SIZE + len)
		return ENXIO;
	memset(&tx, 0, sizeof(tx));
	tx.packet_size = (uint32_t)len;
	tx.queue = (uint8_t)qsel;
	/* main.h DESC_RATE6M and RATEID_G: CCK is invalid on 5 GHz. */
	tx.rate = sc->sc_channel > 14 ? 4 : 0;
	rates = sc->sc_basic_rates & (sc->sc_channel > 14 ? 0xff0U : 0xfffU);
	if (rates != 0)
		tx.rate = (uint8_t)__builtin_ctz(rates);
	if (fixed_rate >= 0)
		tx.rate = (uint8_t)fixed_rate;
	tx.rate_id = tx.rate >= 4 ? RTW88_RATEID_G : RTW88_RATEID_B_20M;
	tx.flags = RTW88_TX_DIS_QSELSEQ | RTW88_TX_USE_RATE |
	    RTW88_TX_DIS_RATE_FB;
	if (q == RTW88_TXQ_MGMT)
		tx.flags |= RTW88_TX_HWSEQ;
	else
		tx.sequence = (uint16_t)((((const uint8_t *)frame)[22] |
		    ((const uint8_t *)frame)[23] << 8) >> 4);
	if (((const uint8_t *)frame)[4] & 0x01)		/* group DA */
		tx.flags |= RTW88_TX_BMC;
	error = rtw88_tx_encode(buf->vaddr, buf->size, &tx);
	if (error != 0)
		return error;
	/* Record the encoded descriptor under the caller's bus interrupt lock.
	 * This is an encoding sample, not an ACK or a hardware rate report. */
	if ((bytes[0] & 0x0c) == 0x08 && (bytes[4] & 1) == 0) {
		unsigned rate = ((const uint8_t *)buf->vaddr)[16] & 0x7f;

		sc->sc_stats.tx_data_encoded++;
		sc->sc_stats.tx_data_rate = (uint8_t)rate;
		sc->sc_stats.tx_data_basic = (uint16_t)sc->sc_basic_rates;
		if (rate < __arraycount(sc->sc_stats.tx_data_rates))
			sc->sc_stats.tx_data_rates[rate]++;
		else
			sc->sc_stats.tx_data_other++;
	}
	memcpy((uint8_t *)buf->vaddr + RTW8822C_TX_PACKET_DESC_SIZE, frame,
	    len);
	sc->sc_ops->dma_sync(sc->sc_bus, buf, BUS_DMASYNC_PREWRITE);
	ring = &sc->sc_ring[rtw88_txq_hw[q].ring];
	bd = (uint8_t *)ring->vaddr + RTW8822C_TX_BUFFER_DESC_SIZE * txq->wp;
	addr = (uint32_t)buf->addr;
	memset(bd, 0, RTW8822C_TX_BUFFER_DESC_SIZE);
	rtw88_put16(bd, RTW8822C_TX_PACKET_DESC_SIZE);
	rtw88_put16(bd + 2, (uint16_t)((len + RTW8822C_TX_PACKET_DESC_SIZE -
	    1) / 128 + 1));
	rtw88_put32(bd + 4, addr);
	rtw88_put16(bd + 8, (uint16_t)len);
	rtw88_put32(bd + 12, addr + RTW8822C_TX_PACKET_DESC_SIZE);
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
	txq->wp = (txq->wp + 1) % RTW88_TX_SLOTS;
	txq->queued++;
	sc->sc_stats.tx_queued++;
	return sc->sc_ops->write_2(sc->sc_bus, rtw88_txq_hw[q].idx,
	    (uint16_t)txq->wp);
}

int
rtw88_tx_raw(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len)
{
	return rtw88_tx_submit(sc, q, frame, len, -1);
}

int
rtw88_tx_fixed(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, const struct rtw88_tx_fixed *fixed)
{
	static const uint8_t rates[] = { 2, 4, 11, 22, 12, 18, 24, 36,
	    48, 72, 96, 108 };
	const uint8_t *bytes = frame;
	unsigned rate;

	if (fixed == NULL || fixed->associated != 1 ||
	    fixed->use_protection > 1 || (fixed->peer_mask & ~0xfffU) != 0 ||
	    fixed->channel == 0 || fixed->channel != sc->sc_channel)
		return EINVAL;
	if (frame == NULL || len < 24)
		return EMSGSIZE;
	if (q == RTW88_TXQ_MGMT || (bytes[0] != 0x08 && bytes[0] != 0x88) ||
	    (bytes[1] & 3) != 1 || (bytes[1] & 0x80) != 0 ||
	    (bytes[4] & 1) != 0 || (bytes[16] & 1) != 0)
		return EOPNOTSUPP;
	for (rate = 0; rate < __arraycount(rates); rate++)
		if (fixed->rate500 == rates[rate])
			break;
	if (rate == __arraycount(rates) ||
	    (fixed->peer_mask & (1U << rate)) == 0)
		return EINVAL;
	switch (fixed->mode) {
	case RTW88_FIXED_11A:
		if (fixed->channel <= 14 || rate < 4)
			return EINVAL;
		break;
	case RTW88_FIXED_11B:
		if (fixed->channel > 14 || rate >= 4)
			return EINVAL;
		break;
	case RTW88_FIXED_11G:
		if (fixed->channel > 14)
			return EINVAL;
		break;
	default:
		return EINVAL;
	}
	/* The current descriptor API has no qualified RTS/CTS protection policy. */
	if (rate >= 4 && fixed->use_protection)
		return EOPNOTSUPP;
	return rtw88_tx_submit(sc, q, frame, len, (int)rate);
}

/* pci.c rtw_pci_tx_isr: completion is the chip's read index, nothing else. */
static void
rtw88_tx_done(struct rtw88_softc *sc, unsigned q)
{
	struct rtw88_txq *txq = &sc->sc_txq[q];
	uint32_t idx;
	unsigned hw, count;

	if (sc->sc_ops->read_4(sc->sc_bus, rtw88_txq_hw[q].idx, &idx) != 0)
		return;
	hw = (idx >> 16) & 0xfff;
	if (hw >= RTW88_TX_SLOTS) {
		sc->sc_stats.tx_dma_errors++;
		return;
	}
	count = hw >= txq->rp ? hw - txq->rp : RTW88_TX_SLOTS - txq->rp + hw;
	if (count > txq->queued) {
		sc->sc_stats.tx_dma_errors++;
		return;
	}
	txq->rp = hw;
	txq->queued -= count;
	sc->sc_stats.tx_done += count;
	if (count != 0 && sc->sc_tx_complete != NULL)
		sc->sc_tx_complete(sc->sc_net_cookie, count);
}

int
rtw88_loopback(struct rtw88_softc *sc, int mode)
{

	if (!sc->sc_running)
		return ENXIO;
	switch (mode) {
	case 0x00: case 0x01: case 0x03: case 0x07: case RTW88_LBMODE_IMMEDIATE:
		break;
	default:
		return EINVAL;
	}
	return sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_CR + 3, (uint8_t)mode);
}

int
rtw88_channel(struct rtw88_softc *sc, unsigned ch)
{
	struct rtw8822c_efuse efuse;
	uint8_t pause;
	int error;

	if (!sc->sc_running)
		return ENXIO;
	/* The calibrated 20 MHz profile has no policy for other channels. */
	if (!((ch >= 1 && ch <= 13) ||
	    (ch >= 36 && ch <= 64 && ch % 4 == 0) ||
	    (ch >= 100 && ch <= 144 && ch % 4 == 0) ||
	    (ch >= 149 && ch <= 165 && (ch - 149) % 4 == 0)))
		return EINVAL;
	error = rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &efuse);
	if (error != 0)
		return error;
	error = sc->sc_ops->read_1(sc->sc_bus, 0x0522, &pause);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, 0xff);
	if (error != 0)
		return error;
	error = rtw8822c_set_channel(sc, ch);
	if (error == 0)
		error = rtw8822c_set_tx_power(sc, &efuse, ch, 12);
	if (error == 0)
		error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, pause);
	/* A failed retune leaves TX paused. The caller must stop the core. */
	if (error == 0)
		sc->sc_channel = ch;
	return error;
}

int
rtw88_diag(struct rtw88_softc *sc, uint32_t *r)
{
	static const uint32_t regs[RTW88_DIAG_REGS] = {
		RTW88_REG_HISR0, RTW88_REG_HIMR0, RTW88_REG_HISR3,
		RTW88_REG_HIMR3, RTW88_REG_RXBD_IDX, RTW88_REG_PCI_CTRL,
		0x3c60, 0x4c60	/* Direct RF18 A/B windows; no RF command writes. */
	};
	unsigned i;
	int error;

	if (!sc->sc_resources_ready || !sc->sc_powered)
		return ENXIO;
	for (i = 0; i < RTW88_DIAG_REGS; i++) {
		error = sc->sc_ops->read_4(sc->sc_bus, regs[i], &r[i]);
		if (error != 0)
			return error;
	}
	return 0;
}

int
rtw88_intr(struct rtw88_softc *sc)
{
	uint32_t s0, s1, s3;

	if (!sc->sc_running)
		return 0;
	if (sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_HISR0, &s0) != 0 ||
	    sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_HISR1, &s1) != 0 ||
	    sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_HISR3, &s3) != 0)
		return 0;
	s0 &= RTW88_IMR0;
	s1 &= RTW88_IMR1;
	s3 &= RTW88_IMR3;
	if ((s0 | s1 | s3) == 0)
		return 0;
	/* Clear every taken cause before work, so MSI edges are not lost. */
	(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HISR0, s0);
	(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HISR1, s1);
	(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HISR3, s3);
	sc->sc_stats.intr++;
	for (unsigned q = 0; q < RTW88_NTXQ; q++)
		if ((s0 & rtw88_txq_hw[q].done) != 0)
			rtw88_tx_done(sc, q);
	if ((s0 & RTW88_IMR_ROK) != 0)
		rtw88_rx(sc);
	return 1;
}

int
rtw88_core_attach(struct rtw88_softc *sc)
{
	/* BK, BE, VI, VO, beacon, management, high, H2C, then MPDU RX. */
	static const size_t sizes[RTW88_NRING] = {
		128 * 16, 256 * 16, 128 * 16, 128 * 16,
		1 * 16, 128 * 16, 128 * 16, 128 * 16, 512 * 8
	};
	int error;

	if (sc->sc_nring != 0 || sc->sc_intr_established ||
	    sc->sc_resources_ready)
		return EBUSY;
	for (unsigned i = 0; i < RTW88_NRING; i++) {
		struct rtw88_dma *ring = &sc->sc_ring[i];

		error = sc->sc_ops->dma_alloc(sc->sc_bus, ring, sizes[i]);
		if (error != 0)
			goto fail;
		sc->sc_nring++;
		/* Validate the whole mapping again at the ownership boundary. */
		error = rtw88_dma_validate(ring->addr, ring->size,
		    RTW8822C_DMA_ADDR_BITS);
		if (error != 0)
			goto fail;
	}
	for (unsigned i = 0; i < RTW88_RX_BUFS; i++) {
		struct rtw88_dma *buf = &sc->sc_rxbuf[i];

		error = sc->sc_ops->dma_alloc(sc->sc_bus, buf,
		    RTW88_RX_BUF_SIZE);
		if (error != 0)
			goto fail;
		sc->sc_nrxbuf++;
		error = rtw88_dma_validate(buf->addr, buf->size,
		    RTW8822C_DMA_ADDR_BITS);
		if (error != 0)
			goto fail;
	}
	for (unsigned q = 0; q < RTW88_NTXQ; q++) {
		struct rtw88_txq *txq = &sc->sc_txq[q];

		for (unsigned i = 0; i < RTW88_TX_SLOTS; i++) {
			error = sc->sc_ops->dma_alloc(sc->sc_bus, &txq->buf[i],
			    RTW88_TX_BUF_SIZE);
			if (error != 0)
				goto fail;
			txq->nbuf++;
			error = rtw88_dma_validate(txq->buf[i].addr,
			    txq->buf[i].size, RTW8822C_DMA_ADDR_BITS);
			if (error != 0)
				goto fail;
		}
	}
	error = sc->sc_ops->intr_establish(sc->sc_bus);
	if (error != 0)
		goto fail;
	sc->sc_intr_established = 1;
	sc->sc_resources_ready = 1;
	return 0;
fail:
	(void)rtw88_core_detach(sc);
	return error;
}

int
rtw88_core_detach(struct rtw88_softc *sc)
{
	int error;

	error = rtw88_stop(sc);
	if (error != 0)
		return error;
	sc->sc_resources_ready = 0;
	if (sc->sc_intr_established) {
		sc->sc_ops->intr_disestablish(sc->sc_bus);
		sc->sc_intr_established = 0;
	}
	for (unsigned q = 0; q < RTW88_NTXQ; q++) {
		struct rtw88_txq *txq = &sc->sc_txq[q];

		while (txq->nbuf != 0) {
			txq->nbuf--;
			sc->sc_ops->dma_free(sc->sc_bus, &txq->buf[txq->nbuf]);
		}
	}
	while (sc->sc_nrxbuf != 0) {
		sc->sc_nrxbuf--;
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_rxbuf[sc->sc_nrxbuf]);
	}
	while (sc->sc_nring != 0) {
		sc->sc_nring--;
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_ring[sc->sc_nring]);
	}
	return 0;
}
