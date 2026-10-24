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
#ifdef RTW88_RFK_EXPERIMENT
#include <dev/ic/rtw8822c_rfk.h>
#endif

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
/* main.c hal.rcr: APP_FCS/MIC/ICV/PHYSTS, VHT_DACK, PKTCTL_DLEN,
 * HTC_LOC, AB/AM/APM. VHT_DACK selects the pinned default ACK policy. */
#define RTW88_RCR		UINT32_C(0xf410400e)
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
	sc->sc_rx_sequence = 0;
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

/* Dedicated startup event slot, not a DMA C2H packet or an H2C command.
 * Protocol: FreeBSD d2145e9b main.c rtw_chip_efuse_enable/rtw_dump_hw_feature,
 * Realtek BSD-3-Clause option. A bounded snapshot cannot detect short firmware
 * writes: the pinned wire contract supplies exactly thirteen payload bytes. */
#define RTW88_REG_C2HEVT 0x01a0

static int
rtw88_hw_feature_fence(struct rtw88_softc *sc, uint32_t gen)
{
	return sc->sc_stop_requested || sc->sc_generation != gen ? EINTR : 0;
}

static int
rtw88_hw_feature_request(struct rtw88_softc *sc, uint32_t gen)
{
	int error;

	if ((error = rtw88_hw_feature_fence(sc, gen)) != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_C2HEVT, 0xfd);
	if (error != 0) {
		sc->sc_stats.hw_io_errors++;
		return error;
	}
	if ((error = rtw88_hw_feature_fence(sc, gen)) != 0)
		return error;
	sc->sc_stats.hw_requests++;
	sc->sc_hw_feature.requested = 1;
	sc->sc_hw_feature.requested_generation = gen;
	return 0;
}

static int
rtw88_hw_feature_read(struct rtw88_softc *sc, uint32_t gen)
{
	struct rtw88_hw_feature report;
	uint8_t id, stable, raw[RTW88_HW_FEATURE_LEN];
	unsigned i;
	int error;

	if ((error = rtw88_hw_feature_fence(sc, gen)) != 0)
		return error;
	error = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_C2HEVT, &id);
	if (error != 0)
		goto io_fail;
	if ((error = rtw88_hw_feature_fence(sc, gen)) != 0)
		return error;
	if (id != 0x19) {
		if (id == 0 || id == 0xfd)
			sc->sc_stats.hw_missing++;
		else
			sc->sc_stats.hw_foreign++;
		return 0; /* Do not consume an event we do not own. */
	}
	for (i = 0; i < sizeof(raw); i++) {
		error = sc->sc_ops->read_1(sc->sc_bus,
		    RTW88_REG_C2HEVT + 2 + i, &raw[i]);
		if (error != 0)
			goto io_fail;
		if ((error = rtw88_hw_feature_fence(sc, gen)) != 0)
			return error;
	}
	error = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_C2HEVT, &stable);
	if (error != 0)
		goto io_fail;
	if ((error = rtw88_hw_feature_fence(sc, gen)) != 0)
		return error;
	if (stable != id) {
		sc->sc_stats.hw_unstable++;
		return 0; /* A replaced event belongs to its other consumer. */
	}
	error = rtw88_hw_feature_decode(raw, sizeof(raw), &report);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_C2HEVT, 0);
	if (error != 0)
		goto io_fail;
	if ((error = rtw88_hw_feature_fence(sc, gen)) != 0)
		return error;
	sc->sc_hw_feature.report = report;
	sc->sc_hw_feature.observed_generation = gen;
	sc->sc_hw_feature.observed = 1;
	sc->sc_stats.hw_reports++;
	return 0;
io_fail:
	sc->sc_stats.hw_io_errors++;
	return error;
}

static void
rtw88_release_fw(struct rtw88_softc *sc)
{

	if (sc->sc_ops->firmware_release != NULL)
		sc->sc_ops->firmware_release(sc->sc_bus);
}

/* REG_CR.MACTXEN and REG_TXPAUSE use pinned rtw88 reg.h definitions.
 * Try both independent controls even when one fails. A failed write can have
 * taken effect; report the first error and never restore either TX control.
 */
int
rtw88_tx_quiet(struct rtw88_softc *sc)
{
	uint8_t cr, check;
	int error, e;

	sc->sc_stop_requested = 1;
	sc->sc_phy.channel_valid = 0;
	/* Also cancel a start that yielded inside the firmware loader. */
	sc->sc_generation++;
	rtw88_ra_invalidate(sc);
#ifdef RTW88_RFK_EXPERIMENT
	sc->sc_power_program.valid = 0;
	sc->sc_operating.prepared = sc->sc_operating.sealed = 0;
#endif
	if (!sc->sc_resources_ready || !sc->sc_running || !sc->sc_started)
		return ENXIO;
	error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, 0xff);
	e = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_CR, &cr);
	if (e == 0)
		e = sc->sc_ops->write_1(sc->sc_bus, RTW88_REG_CR, cr & ~0x40);
	if (error == 0)
		error = e;
	e = sc->sc_ops->read_1(sc->sc_bus, 0x0522, &check);
	if (e == 0 && check != 0xff)
		e = EIO;
	if (error == 0)
		error = e;
	e = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_CR, &check);
	if (e == 0 && (check & 0x40) != 0)
		e = EIO;
	if (error == 0)
		error = e;
	return error;
}

int
rtw88_stop(struct rtw88_softc *sc)
{
	int error;

#ifdef RTW88_RFK_EXPERIMENT
	sc->sc_power_program.valid = 0;
	sc->sc_operating.prepared = sc->sc_operating.sealed = 0;
#endif
	sc->sc_stop_requested = 1;
	memset(&sc->sc_mailbox, 0, sizeof(sc->sc_mailbox));
	memset(&sc->sc_phy, 0, sizeof(sc->sc_phy));
	memset(&sc->sc_hw_feature, 0, sizeof(sc->sc_hw_feature));
	rtw88_ra_invalidate(sc);
	sc->sc_linked = 0;
	sc->sc_stats.ra_report_valid = 0;
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

/*
 * Live admission mirrors the passive, non-DFS net80211 channel set. The
 * only wide tuple is the qualified VHT80 target; channel 64 stays closed.
 */
static int
rtw88_channel_admit(const struct rtw88_channel_tuple *tuple)
{
	unsigned ch = tuple->primary;

	if (tuple->width_mhz == 20 && tuple->center == ch &&
	    ((ch >= 1 && ch <= 13) ||
	    (ch >= 36 && ch <= 64 && ch % 4 == 0 && ch != 64) ||
	    (ch >= 100 && ch <= 144 && ch % 4 == 0) ||
	    (ch >= 149 && ch <= 165 && (ch - 149) % 4 == 0)))
		return 0;
#ifdef RTW88_VHT80
	if (tuple->width_mhz == 80 && ch == 36 && tuple->center == 42)
		return 0;
#endif
	return EOPNOTSUPP;
}

int
rtw88_start(struct rtw88_softc *sc)
{
	const struct rtw88_channel_tuple default_channel = {
		RTW88_DEFAULT_CHANNEL, RTW88_DEFAULT_CHANNEL, 20
	};

	return rtw88_start_tuple(sc, &default_channel);
}

int
rtw88_start_tuple(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *target)
{
	struct rtw88_channel_tuple admitted;
	const struct rtw88_channel_tuple *tuple = &admitted;
	const uint8_t *fw = NULL;
	const char *stage = "identity";
	size_t fw_len = 0;
	struct rtw88_fw_info info;
	struct rtw8822c_efuse efuse;
	uint32_t gen;
	int error;

	if (target == NULL)
		return EINVAL;
	/* Program exactly the admitted copy; firmware loading may sleep. */
	admitted = *target;
	if (rtw88_channel_admit(&admitted) != 0)
		return EOPNOTSUPP;
#ifdef RTW88_RFK_EXPERIMENT
	if (rtw8822c_rfk_blocked())
		return EBUSY;
#endif
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
#ifdef RTW88_RFK_EXPERIMENT
	sc->sc_power_program.valid = 0;
	sc->sc_operating.prepared = sc->sc_operating.sealed = 0;
#endif
	sc->sc_phy.channel_valid = 0;
	gen = sc->sc_generation;
	error = sc->sc_ops->firmware_load(sc->sc_bus, RTW88_FW_DRV,
	    RTW88_FW_IMG, &fw, &fw_len);
	if (error != 0)
		return error;
	if (gen != sc->sc_generation) {
		rtw88_release_fw(sc);
		return EINTR;
	}
	error = rtw88_fw_parse(fw, fw_len, &info);
	if (error != 0) {
		rtw88_release_fw(sc);
		return error;
	}
	sc->sc_generation++;
	gen = sc->sc_generation;
	memset(&sc->sc_hw_feature, 0, sizeof(sc->sc_hw_feature));
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
	stage = "HW feature request";
	error = rtw88_hw_feature_request(sc, gen);
	if (error != 0)
		goto fail;
	stage = "firmware download";
	error = rtw88_fw_download(sc, gen, fw, &info);
	if (error != 0)
		goto fail;
	stage = "HW feature report";
	error = rtw88_hw_feature_read(sc, gen);
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
	/* Optional metadata: a failed read remains visible and disables CCK RSSI.
	 * Correct DATA and OFDM observations do not require this CCK context. */
	(void)rtw8822c_signal_cache(sc);
	/* rtw_core_start: RCR = hal.rcr, accepting any BSSID's beacons. */
	error = sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_RCR, RTW88_RCR);
	if (error != 0)
		goto fail;
	stage = "channel setup";
	error = rtw8822c_set_channel_tuple(sc, tuple);
	if (error != 0)
		goto fail;
	if (sc->sc_stop_requested || sc->sc_generation != gen) {
		error = EINTR;
		goto fail;
	}
	stage = "TX power";
	error = rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &efuse);
	if (error == 0)
		error = rtw8822c_set_tx_power_tuple(sc, &efuse, tuple, 12);
	if (error != 0)
		goto fail;
	sc->sc_channel = tuple->primary;
	sc->sc_phy.channel = *tuple;
	if (sc->sc_stop_requested || sc->sc_generation != gen) {
		error = EINTR;
		goto fail;
	}
#ifndef RTW88_RFK_EXPERIMENT
	sc->sc_phy.channel_valid = 1;
#endif
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
	sc->sc_ra.error = 0;
	sc->sc_stats.ra_report_valid = 0;
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

/* Snapshot all needed boxes before mutation. Busy is not an IO error.
 * Caller owns the managed worker and serializes with PCI IRQ/stop. */
int
rtw88_fw_mailbox_begin(struct rtw88_softc *sc, unsigned count, int *ready)
{
	uint8_t occupied;
	uint32_t gen;
	unsigned mask = 0, i, box;
	int error;

	if (ready == NULL || count > 3)
		return EINVAL;
	*ready = 0;
	if (count == 0) {
		*ready = 1;
		return 0;
	}
	if (sc->sc_mailbox.active)
		return EBUSY;
	if (!sc->sc_started || !sc->sc_rf_enabled || sc->sc_stop_requested)
		return ENXIO;
	gen = sc->sc_generation;
	box = sc->sc_h2c_box & 3;
	for (i = 0; i < count; i++)
		mask |= 1U << ((box + i) & 3);
	error = sc->sc_ops->read_1(sc->sc_bus, RTW88_REG_HMETFR, &occupied);
	if (error != 0)
		return error;
	if (sc->sc_stop_requested || gen != sc->sc_generation ||
	    box != (sc->sc_h2c_box & 3))
		return ESTALE;
	if ((occupied & mask) != 0)
		return 0;
	sc->sc_mailbox.generation = gen;
	sc->sc_mailbox.box = (uint8_t)box;
	sc->sc_mailbox.total = (uint8_t)count;
	sc->sc_mailbox.sent = 0;
	sc->sc_mailbox.active = 1;
	*ready = 1;
	return 0;
}

int
rtw88_fw_mailbox_end(struct rtw88_softc *sc, int error)
{

	if (sc->sc_mailbox.active && error == 0 &&
	    (sc->sc_mailbox.generation != sc->sc_generation ||
	    sc->sc_mailbox.sent != sc->sc_mailbox.total))
		error = EPROTO;
	memset(&sc->sc_mailbox, 0, sizeof(sc->sc_mailbox));
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

	if (!sc->sc_started || !sc->sc_rf_enabled || sc->sc_stop_requested)
		return ENXIO;
	if (size > 0 && payload == NULL)
		return EINVAL;
	if (size > RTW88_FW_CMD_MAX)
		return EMSGSIZE;
	gen = sc->sc_generation;
	box.reg = RTW88_REG_HMETFR;
	box.mask = (uint32_t)1 << (sc->sc_h2c_box & 3);
	box.value = 0;
	if (sc->sc_mailbox.active) {
		if (sc->sc_mailbox.generation != gen ||
		    sc->sc_mailbox.sent >= sc->sc_mailbox.total ||
		    ((sc->sc_mailbox.box + sc->sc_mailbox.sent) & 3) !=
		    (sc->sc_h2c_box & 3))
			return ESTALE;
		error = rtw88_ready8(sc, &box);
		error = error < 0 ? -error : error != 0 ? EBUSY : 0;
	} else {
		error = rtw88_poll(sc, gen, RTW88_COMMAND_US, rtw88_ready8, &box);
	}
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
	if (sc->sc_mailbox.active && gen != sc->sc_generation)
		return ESTALE;
	error = sc->sc_ops->write_4(sc->sc_bus, ex_reg, w1);
	if (error != 0)
		return error;
	if (sc->sc_mailbox.active && gen != sc->sc_generation)
		return ESTALE;
	error = sc->sc_ops->write_4(sc->sc_bus, box_reg, w0);
	if (error != 0)
		return error;
	sc->sc_h2c_box = (sc->sc_h2c_box + 1) & 3;
	if (sc->sc_mailbox.active)
		sc->sc_mailbox.sent++;
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

/*
 * Pinned fw.h C2H_RA_RPT: ID/sequence and seven payload bytes. The wire
 * has no association token. These are unattributed firmware observations,
 * never ACKs or input to host peer/rate policy.
 */
static void
rtw88_ra_report(struct rtw88_softc *sc, const uint8_t *p, size_t len)
{
	struct rtw88_stats *st = &sc->sc_stats;
	unsigned rate;

	if (len < 9) {
		st->ra_malformed++;
		return;
	}
	if (len > 9)
		st->ra_trailing++;
	if (p[3] != 0) {
		st->ra_foreign++;
		return;
	}
	rate = p[2] & 0x7f;
	/* Pinned DESC_RATEMCS0..7 are 12..19; no SGI or wider BW here. */
	if (rate >= 20 || (p[2] & 0x80) != 0 || p[8] != 0) {
		st->ra_unsupported++;
		return;
	}
	st->ra_reports++;
	st->ra_unattributed++;
	if (!sc->sc_ra.active)
		st->ra_inactive++;
	if (sc->sc_ra.configured && (sc->sc_ra.mask & (1U << rate)) == 0)
		st->ra_outside_mask++;
	st->ra_report_rate = (uint8_t)rate;
	st->ra_report_macid = p[3];
	st->ra_report_bw = p[8];
	st->ra_observed_generation = sc->sc_generation;
	st->ra_report_valid = 1;
}

/* One received buffer: classify, count, and keep every error visible. */
static void
rtw88_rx_one(struct rtw88_softc *sc, unsigned idx, uint64_t sequence)
{
	struct rtw88_dma *buf = &sc->sc_rxbuf[idx];
	struct rtw88_rx_info rx;
	const uint8_t *p;

	sc->sc_ops->dma_sync(sc->sc_bus, buf, BUS_DMASYNC_POSTREAD);
	if (rtw88_rx_decode(buf->vaddr, RTW88_RX_BUF_SIZE, &rx) != 0) {
		sc->sc_stats.rx_errors++;
		return;
	}
	rx.sequence = sequence;
	rx.generation = sc->sc_generation;
	p = (const uint8_t *)buf->vaddr + rx.payload_offset;
	if ((rx.flags & RTW88_RX_C2H) != 0) {
		if (rx.packet_size < 2) {
			sc->sc_stats.rx_errors++;
			return;
		}
		sc->sc_stats.rx_c2h++;
		sc->sc_stats.last_c2h_id = p[0];
		sc->sc_stats.last_c2h_seq = p[1];
		if (p[0] == 0x0c)
			rtw88_ra_report(sc, p, rx.packet_size);
		return;
	}
	if ((rx.flags & (RTW88_RX_CRC_ERROR | RTW88_RX_ICV_ERROR)) != 0) {
		sc->sc_stats.rx_errors++;
		return;
	}
	(void)rtw88_rx_signal_decode_tuple((const uint8_t *)buf->vaddr +
	    rx.driver_info_offset, rx.driver_info_size, &rx, &sc->sc_phy,
	    sc->sc_generation, &sc->sc_phy.channel, &rx.signal);
	sc->sc_stats.signal = rx.signal;
	sc->sc_stats.signal_reasons[rx.signal.reason]++;
	if (rx.packet_size < RTW88_MIN_FRAME) {
		sc->sc_stats.rx_short++;
		return;
	}
	/* The diagnostic counters include valid control frames. */
	sc->sc_stats.rx_frames++;
	if (p[0] == 0x80 && rx.packet_size >= RTW88_BEACON_IES)
		rtw88_rx_beacon(sc, p, rx.packet_size);
	/* RCR.APP_FCS is set. Never expose the four FCS bytes to net80211. */
	if (sc->sc_rx_frame != NULL && (rx.packet_size >= 24 + 4 ||
	    (rx.packet_size == 20 + 4 && p[0] == 0x84 && (p[1] & ~0x08) == 0 &&
	     p[16] == 4 && (p[17] & 0x8f) == 0 && (p[18] & 15) == 0))) {
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
	uint64_t sequence;
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
		/* Every consumed slot advances, including rejected tags and C2H. */
		sequence = sc->sc_rx_sequence == UINT64_MAX ? 0 :
		    ++sc->sc_rx_sequence;
		bd = (uint8_t *)ring->vaddr + RTW8822C_RX_BUFFER_DESC_SIZE * rp;
		tag = (uint16_t)(bd[2] | bd[3] << 8);
		if (tag != sc->sc_rx_tag)
			sc->sc_stats.rx_tag_errors++;
		else
			rtw88_rx_one(sc, rp, sequence);
		sc->sc_rx_tag = (sc->sc_rx_tag + 1) % RTW88_RX_TAG_MAX;
		rtw88_rx_arm(sc, rp);
		rp = (rp + 1) % sc->sc_nrxbuf;
	}
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
	sc->sc_rx_rp = rp;
	(void)sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_RXBD_IDX, (uint16_t)rp);
}

/* Read the completed DMA producer without consuming or publishing slots. */
int
rtw88_rx_frontier(struct rtw88_softc *sc, uint32_t expected_generation,
    uint64_t *frontier)
{
	const struct rtw88_dma *ring;
	unsigned nrxbuf, rp, hw, count;
	uint32_t idx;
	int error;

	if (frontier == NULL)
		return EINVAL;
	*frontier = 0;
	if (sc == NULL || sc->sc_ops == NULL || sc->sc_ops->read_4 == NULL)
		return EINVAL;
	if (!sc->sc_started || !sc->sc_running || !sc->sc_resources_ready ||
	    sc->sc_stop_requested || !sc->sc_dma_master || sc->sc_dma_error != 0)
		return ENXIO;
	if (sc->sc_generation != expected_generation)
		return EINTR;
	nrxbuf = sc->sc_nrxbuf;
	rp = sc->sc_rx_rp;
	ring = &sc->sc_ring[RTW88_RING_RX];
	if (nrxbuf == 0 || nrxbuf > RTW88_RX_BUFS || rp >= nrxbuf ||
	    ring->vaddr == NULL ||
	    ring->size < nrxbuf * RTW8822C_RX_BUFFER_DESC_SIZE ||
	    (ring->size % RTW8822C_RX_BUFFER_DESC_SIZE) != 0)
		return EINVAL;
	if (sc->sc_rx_sequence == UINT64_MAX)
		return EOVERFLOW;
	error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_RXBD_IDX, &idx);
	if (error != 0)
		return error;
	if (sc->sc_generation != expected_generation)
		return EINTR;
	if (!sc->sc_started || !sc->sc_running || !sc->sc_resources_ready ||
	    sc->sc_stop_requested || !sc->sc_dma_master || sc->sc_dma_error != 0)
		return ENXIO;
	hw = (idx >> 16) & 0xfff;
	if (hw >= nrxbuf)
		return EINVAL;
	count = hw >= rp ? hw - rp : nrxbuf - rp + hw;
	if (count >= UINT64_MAX - sc->sc_rx_sequence)
		return EOVERFLOW;
	*frontier = sc->sc_rx_sequence + count;
	return 0;
}

unsigned
rtw88_tx_available(struct rtw88_softc *sc, unsigned q)
{
	const struct rtw88_txq *txq;

	if (q >= RTW88_NTXQ || !sc->sc_running || sc->sc_stop_requested ||
	    sc->sc_edca_error != 0 ||
	    sc->sc_ra.error != 0)
		return 0;
	txq = &sc->sc_txq[q];
	return txq->rp > txq->wp ? txq->rp - txq->wp - 1 :
	    RTW88_TX_SLOTS - txq->wp + txq->rp - 1;
}

/*
 * Port0 fields and mailbox layouts are from pinned rtw88 main.c,
 * fw.c/fw.h, sec.c and reg.h. This host-managed legacy station does
 * not enable hardware crypto or LPS; RA is configured separately.
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
	if (sc->sc_ra.error != 0)
		return sc->sc_ra.error;
	if (!sc->sc_running || !sc->sc_started || sc->sc_stop_requested)
		return ENXIO;
	if (sc->sc_channel > 14 && (basic_rates & 0xf) != 0)
		return EINVAL;
	rtw88_ra_invalidate(sc);
	sc->sc_linked = 0;
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
	if (io.error == 0) {
		sc->sc_basic_rates = basic_rates;
		sc->sc_linked = linked;
	}
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
	if (sc->sc_ra.error != 0)
		return sc->sc_ra.error;
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

/* Pinned fw.c/fw.h RA_INFO register command; legacy, HT20/1SS or VHT80/1SS. */
void
rtw88_ra_invalidate(struct rtw88_softc *sc)
{
	sc->sc_ra.active = 0;
	sc->sc_ra.configured = 0;
}

static int
rtw88_ht20_policy_check(const struct rtw88_ht20_policy *p, unsigned operation)
{

	if (p->qualification != RTW88_HT20_PROT_MF20_NO_RTS ||
	    (operation != 0 && operation != 6) ||
	    (p->peer_cap & 0x1c) != 0x0c ||
	    (p->secondary != 0 && p->secondary != 5 && p->secondary != 7) ||
	    p->mtu < RTW88_HT20_MTU_MIN || p->mtu > RTW88_HT20_MTU_MAX ||
	    p->max_mpdu != p->mtu + RTW88_HT20_TX_OVERHEAD ||
	    p->max_mpdu > RTW88_TX_BUF_SIZE - RTW8822C_TX_PACKET_DESC_SIZE ||
	    p->rts_threshold < p->max_mpdu + RTW88_HT20_FCS_SIZE ||
	    p->rts_threshold > RTW88_HT20_THRESHOLD_MAX)
		return EOPNOTSUPP;
	return 0;
}

/* Legacy, HT20 and every current descriptor path use the 20 MHz tuple. */
static int
rtw88_channel_narrow(const struct rtw88_softc *sc)
{
	return sc->sc_phy.channel.width_mhz == 20 &&
	    sc->sc_phy.channel.primary == sc->sc_channel &&
	    sc->sc_phy.channel.center == sc->sc_channel;
}

#ifdef RTW88_VHT80
/*
 * The one qualified wide target: non-DFS primary36/center42/80, with the
 * actual PHY and power program committed in this generation.
 */
int
rtw88_vht80_committed(const struct rtw88_softc *sc)
{
	return sc->sc_phy.channel_valid && sc->sc_channel == 36 &&
	    sc->sc_phy.channel.primary == 36 &&
	    sc->sc_phy.channel.center == 42 &&
	    sc->sc_phy.channel.width_mhz == 80;
}
#endif

int
rtw88_ra_configure(struct rtw88_softc *sc, const struct rtw88_ra_config *cfg)
{
	struct rtw88_ra_state ra = { 0 };
	uint8_t payload[7] = { 0 };
	unsigned mask;
	int error, tuple_ok;

	if (cfg == NULL || cfg->epoch == 0 || cfg->use_protection > 1 ||
	    cfg->channel == 0 || cfg->channel != sc->sc_channel ||
	    cfg->common_rates == 0 || (cfg->common_rates & ~0xfffU) != 0)
		return EINVAL;
	if (cfg->phy == RTW88_RA_HT20_1SS) {
		if (cfg->mode != RTW88_FIXED_11A || cfg->channel <= 14 ||
		    cfg->use_protection != 0 || cfg->vht_tx_map != 0 ||
		    rtw88_ht20_policy_check(&cfg->ht, cfg->ht_operation) != 0 ||
		    cfg->profile_generation == 0 || cfg->ht_mcs == 0 ||
		    (cfg->ht_mcs & ~0xffU) != 0)
			return EOPNOTSUPP;
#ifdef RTW88_VHT80
	} else if (cfg->phy == RTW88_RA_VHT80_1SS) {
		/* No HT-mixed entry; keep the mandatory 6 Mbps fallback. */
		if (cfg->mode != RTW88_FIXED_11A || cfg->channel != 36 ||
		    cfg->use_protection != 0 ||
		    rtw88_ht20_policy_check(&cfg->ht, cfg->ht_operation) != 0 ||
		    cfg->profile_generation == 0 || cfg->ht_mcs != 0 ||
		    cfg->vht_tx_map != RTW88_VHT_MAP_NSS1_MCS7 ||
		    (cfg->common_rates & 0x10) == 0)
			return EOPNOTSUPP;
#endif
	} else if (cfg->phy != RTW88_RA_LEGACY || cfg->ht_mcs != 0 ||
	    cfg->ht_operation != 0 || cfg->profile_generation != 0 ||
	    cfg->ht.peer_cap != 0 || cfg->ht.mtu != 0 || cfg->ht.max_mpdu != 0 ||
	    cfg->ht.rts_threshold != 0 || cfg->ht.secondary != 0 ||
	    cfg->ht.qualification != 0 || cfg->vht_tx_map != 0) {
		return EINVAL;
	}
	mask = cfg->common_rates;
	switch (cfg->mode) {
	case RTW88_FIXED_11A:
		if (cfg->channel <= 14 || (mask & 0xf) != 0)
			return EINVAL;
		break;
	case RTW88_FIXED_11B:
		if (cfg->channel > 14 || (mask & ~0xfU) != 0)
			return EINVAL;
		break;
	case RTW88_FIXED_11G:
		if (cfg->channel > 14)
			return EINVAL;
		break;
	default:
		return EINVAL;
	}
	if (sc->sc_edca_error != 0)
		return sc->sc_edca_error;
	if (sc->sc_ra.error != 0)
		return sc->sc_ra.error;
	if (!sc->sc_running || !sc->sc_started || !sc->sc_linked ||
	    sc->sc_stop_requested)
		return ENXIO;
	/* Each RA entry is qualified only for its committed channel tuple. */
	tuple_ok = rtw88_channel_narrow(sc);
#ifdef RTW88_VHT80
	if (cfg->phy == RTW88_RA_VHT80_1SS)
		tuple_ok = rtw88_vht80_committed(sc);
#endif
	if (!tuple_ok)
		return EOPNOTSUPP;
	ra.epoch = cfg->epoch;
	ra.generation = sc->sc_generation;
	ra.channel = cfg->channel;
	ra.common_rates = mask;
	ra.configured = 1;
	ra.phy = cfg->phy;
	ra.ht_mcs = cfg->ht_mcs;
	ra.ht_operation = cfg->ht_operation;
	ra.profile_generation = cfg->profile_generation;
	ra.ht = cfg->ht;
	ra.vht_tx_map = cfg->vht_tx_map;
	if (cfg->phy == RTW88_RA_HT20_1SS) {
		ra.rate_id = 5; /* Pinned RTW_RATEID_GN_N1SS, 5 GHz only. */
		mask = (unsigned)cfg->ht_mcs << 12 | (mask & 0x30);
#ifdef RTW88_VHT80
	} else if (cfg->phy == RTW88_RA_VHT80_1SS) {
		ra.rate_id = 10; /* Pinned RTW_RATEID_ARFR1_AC_1SS. */
		/* main.c get_vht_ra_mask: NSS1 code0 is bits12..19. */
		mask = 0xffU << 12 | (mask & 0x10);
#endif
	} else if ((mask & ~0xfU) == 0)
		ra.rate_id = RTW88_RATEID_B_20M;
	else if ((mask & 0xf) == 0)
		ra.rate_id = RTW88_RATEID_G;
	else {
		ra.rate_id = 6; /* Pinned RTW_RATEID_BG. */
		mask &= 0xff5;
	}
	ra.mask = mask;
	if (cfg->use_protection) {
		/* No qualified RTS policy: keep a visible legacy CCK path. */
		if ((sc->sc_basic_rates & cfg->common_rates & 0xf) == 0)
			return EOPNOTSUPP;
		ra.protection_limited = 1;
		sc->sc_ra = ra;
		return 0;
	}
	payload[1] = ra.rate_id | (1U << 5); /* Initial RA level1, SGI0. */
	payload[2] = 0x40; /* DIS_PT, BW20, LDPC0, NO_UPDATE0, VHT0. */
	if (ra.phy == RTW88_RA_VHT80_1SS)
		payload[2] |= 0x12; /* VHT1, BW80. */
	payload[3] = (uint8_t)mask;
	payload[4] = (uint8_t)(mask >> 8);
	payload[5] = (uint8_t)(mask >> 16);
	payload[6] = (uint8_t)(mask >> 24);
	error = rtw88_fw_command(sc, 0x40, payload, sizeof(payload));
	if (error != 0) {
		rtw88_ra_invalidate(sc);
		sc->sc_ra.error = error;
		sc->sc_stats.ra_command_errors++;
		return error;
	}
	ra.active = 1;
	sc->sc_ra = ra;
	sc->sc_stats.ra_commands++;
	return 0;
}

static int
rtw88_tx_submit(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, int fixed_rate, unsigned ra_mode)
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
#ifdef RTW88_VHT80
	int vht80 = 0;
#endif

	if (q >= RTW88_NTXQ)
		return EINVAL;
	if (sc->sc_edca_error != 0)
		return sc->sc_edca_error;
	if (sc->sc_ra.error != 0)
		return sc->sc_ra.error;
	if (!sc->sc_resources_ready || !sc->sc_running || !sc->sc_started ||
	    sc->sc_stop_requested)
		return ENXIO;
	if (rtw88_channel_narrow(sc)) {
		if (ra_mode == 1 && sc->sc_ra.phy == RTW88_RA_VHT80_1SS)
			return EOPNOTSUPP;
	} else {
#ifdef RTW88_VHT80
		/*
		 * Qualified VHT80 formats only. Firmware-RA data is BW80 VHT,
		 * or non-HT duplicate after its 6 Mbps fallback. Every other
		 * frame is non-HT OFDM on primary20, placed by DATA_SC: raw
		 * frames use 5 GHz basic OFDM and tx_fixed rejects 11A CCK.
		 */
		if (!rtw88_vht80_committed(sc) || ra_mode == 2 ||
		    (ra_mode == 1 && sc->sc_ra.phy != RTW88_RA_VHT80_1SS))
			return EOPNOTSUPP;
		vht80 = ra_mode == 1;
#else
		/* Default descriptor encoder and DMA engine are 20 MHz only. */
		return EOPNOTSUPP;
#endif
	}
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
	if (ra_mode == 1) {
		tx.mac_id = 0;
		tx.rate_id = sc->sc_ra.rate_id;
		tx.rate = (uint8_t)__builtin_ctz(sc->sc_ra.mask);
		tx.flags &= ~(RTW88_TX_USE_RATE | RTW88_TX_DIS_RATE_FB);
	}
#ifdef RTW88_VHT80
	if (vht80) {
		/* tx.c get_highest_vht_tx_rate seed, not the lowest RA bit. */
		tx.rate = 0x33; /* DESC_RATEVHT1SS_MCS7. */
		tx.bandwidth = 2; /* DATA_BW 80 MHz. */
	}
#endif
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
		if (ra_mode == 1)
			sc->sc_stats.tx_ra_encoded++;
		else if (ra_mode == 2)
			sc->sc_stats.tx_ra_protection_limited++;
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
	error = sc->sc_ops->write_2(sc->sc_bus, rtw88_txq_hw[q].idx,
	    (uint16_t)txq->wp);
	if (error == 0)
		sc->sc_stats.tx_posted[q]++;
	return error;
}

int
rtw88_tx_raw(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len)
{
	return rtw88_tx_submit(sc, q, frame, len, -1, 0);
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
	return rtw88_tx_submit(sc, q, frame, len, (int)rate, 0);
}

int
rtw88_tx_auto(struct rtw88_softc *sc, unsigned q, const void *frame,
    size_t len, uint64_t epoch, unsigned use_protection)
{
	const uint8_t *bytes = frame;
	unsigned basic, rate;

	if (use_protection > 1)
		return EINVAL;
	if (frame == NULL || len < 24)
		return EMSGSIZE;
	if (q == RTW88_TXQ_MGMT || (bytes[0] != 0x08 && bytes[0] != 0x88) ||
	    (bytes[1] & 3) != 1 || (bytes[1] & 0x80) != 0 ||
	    (bytes[4] & 1) != 0 || (bytes[16] & 1) != 0)
		return EOPNOTSUPP;
	if (sc->sc_ra.error != 0)
		return sc->sc_ra.error;
	if (!sc->sc_linked || !sc->sc_ra.configured ||
	    sc->sc_ra.epoch != epoch ||
	    sc->sc_ra.generation != sc->sc_generation ||
	    sc->sc_ra.channel != sc->sc_channel)
		return EBUSY;
	if (sc->sc_ra.phy == RTW88_RA_HT20_1SS ||
	    sc->sc_ra.phy == RTW88_RA_VHT80_1SS) {
		if (use_protection ||
		    rtw88_ht20_policy_check(&sc->sc_ra.ht, sc->sc_ra.ht_operation) != 0 ||
		    (bytes[1] & 0x04) != 0 || (bytes[22] & 15) != 0 ||
		    (bytes[0] == 0x88 &&
		     (len < 26 || (bytes[24] & 0xf0) != 0 || bytes[25] != 0)))
			return EOPNOTSUPP;
		if (len > sc->sc_ra.ht.max_mpdu)
			return EMSGSIZE;
	}
	if (use_protection) {
		sc->sc_ra.active = 0;
		sc->sc_ra.protection_limited = 1;
	}
	if (sc->sc_ra.protection_limited) {
		basic = sc->sc_basic_rates & 0xfff;
		if (basic == 0)
			return EOPNOTSUPP;
		rate = (unsigned)__builtin_ctz(basic);
		if (rate >= 4 || (sc->sc_ra.common_rates & (1U << rate)) == 0)
			return EOPNOTSUPP;
		return rtw88_tx_submit(sc, q, frame, len, -1, 2);
	}
	if (!sc->sc_ra.active)
		return EBUSY;
	return rtw88_tx_submit(sc, q, frame, len, -1, 1);
}

/* pci.c rtw_pci_tx_isr: completion is the chip's read index, nothing else. */
static int
rtw88_tx_reclaim(struct rtw88_softc *sc, unsigned q)
{
	struct rtw88_txq *txq = &sc->sc_txq[q];
	uint32_t idx;
	unsigned hw, count;
	int error;

	error = sc->sc_ops->read_4(sc->sc_bus, rtw88_txq_hw[q].idx, &idx);
	if (error != 0) {
		sc->sc_stats.tx_dma_errors++;
		return error;
	}
	hw = (idx >> 16) & 0xfff;
	if (hw >= RTW88_TX_SLOTS) {
		sc->sc_stats.tx_dma_errors++;
		return EIO;
	}
	count = hw >= txq->rp ? hw - txq->rp : RTW88_TX_SLOTS - txq->rp + hw;
	if (count > txq->queued) {
		sc->sc_stats.tx_dma_errors++;
		return EIO;
	}
	txq->rp = hw;
	txq->queued -= count;
	sc->sc_stats.tx_done += count;
	if (count != 0 && sc->sc_tx_complete != NULL)
		sc->sc_tx_complete(sc->sc_net_cookie, count);
	return 0;
}

/* Same PCI-serialized device-RP decoder as IRQ completion. This is host DMA
 * ownership only: never infer RF idle, air ACK or calibration readiness. */
int
rtw88_tx_drain_snapshot(struct rtw88_softc *sc, unsigned *pending)
{
	unsigned q;
	int error;

	if (pending == NULL)
		return EINVAL;
	*pending = 0;
	if (!sc->sc_started || !sc->sc_running || sc->sc_stop_requested)
		return ENXIO;
	for (q = 0; q < RTW88_NTXQ; q++) {
		struct rtw88_txq *txq = &sc->sc_txq[q];

		if (txq->queued >= RTW88_TX_SLOTS ||
		    txq->wp >= RTW88_TX_SLOTS || txq->rp >= RTW88_TX_SLOTS) {
			sc->sc_stats.tx_dma_errors++;
			return EIO;
		}
		if (txq->queued == 0)
			continue;
		error = rtw88_tx_reclaim(sc, q);
		if (error != 0)
			return error;
		*pending += txq->queued;
	}
	return 0;
}

int
rtw88_loopback(struct rtw88_softc *sc, int mode)
{

	if (!sc->sc_running || sc->sc_stop_requested)
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
rtw88_channel_apply(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple)
{
	struct rtw8822c_efuse efuse;
	struct rtw8822c_channel_geometry geometry;
	uint32_t gen;
	uint8_t pause;
	unsigned ch;
	int error;

	if (!sc->sc_running || sc->sc_stop_requested)
		return ENXIO;
	if (tuple == NULL)
		return EINVAL;
	/* A warm call never changes RF width; rtw88_start_tuple owns that. */
	if (sc->sc_phy.channel.width_mhz != tuple->width_mhz)
		return EOPNOTSUPP;
	if ((error = rtw8822c_channel_geometry(tuple, &geometry)) != 0)
		return error;
	ch = tuple->primary;
	if (rtw88_channel_admit(tuple) != 0)
		return EOPNOTSUPP;
	error = rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &efuse);
	if (error != 0)
		return error;
	sc->sc_phy.channel_valid = 0;
	rtw88_ra_invalidate(sc);
#ifdef RTW88_RFK_EXPERIMENT
	sc->sc_operating.prepared = sc->sc_operating.sealed = 0;
	sc->sc_power_program.valid = 0;
#endif
	gen = sc->sc_generation;
	error = sc->sc_ops->read_1(sc->sc_bus, 0x0522, &pause);
	if (error != 0)
		return error; /* No write was attempted. A warm retry is safe. */
	/* An attempted write can mutate hardware even when it returns an error. */
	error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, 0xff);
	if (error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		error = EINTR;
	if (error == 0)
		error = rtw8822c_set_channel_tuple(sc, tuple);
	if (error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		error = EINTR;
	if (error == 0)
		error = rtw8822c_set_tx_power_tuple(sc, &efuse, tuple, 12);
	if (error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		error = EINTR;
	if (error == 0)
		error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, pause);
	if (error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		error = EINTR;
	if (error != 0) {
		/*
		 * The lifecycle owner must stop outside the interrupt lock.
		 * Keep host TX closed even if TXPAUSE was not accepted.
		 */
		sc->sc_stop_requested = 1;
		return error;
	}
	sc->sc_channel = ch;
	sc->sc_phy.channel = *tuple;
#ifndef RTW88_RFK_EXPERIMENT
	sc->sc_phy.channel_valid = 1;
#endif
	return 0;
}

int
rtw88_channel(struct rtw88_softc *sc, unsigned ch)
{
	const struct rtw88_channel_tuple tuple = { ch, ch, 20 };

	return rtw88_channel_apply(sc, &tuple);
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
			(void)rtw88_tx_reclaim(sc, q);
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

#ifdef RTW88_RFK_EXPERIMENT
	if (rtw8822c_rfk_blocked())
		return EBUSY;
#endif

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

#ifdef RTW88_RFK_EXPERIMENT
	if (rtw8822c_rfk_active(sc))
		return EBUSY;
#endif
	error = rtw88_stop(sc);
	if (error != 0)
		return error;
#ifdef RTW88_RFK_EXPERIMENT
	error = rtw8822c_rfk_free(sc);
	if (error != 0)
		return error;
#endif
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
