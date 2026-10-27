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
#include <dev/ic/rtw88_io.h>
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
	[RTW88_TXQ_MGMT] = { RTW88_TX_RING_MGMT, 0x0380, 0x03b0,
	    RTW88_IMR_MGNTDOK },
	[RTW88_TXQ_BE] = { RTW88_TX_RING_BE, 0x0388, 0x03a8,
	    RTW88_IMR_BEDOK },
	[RTW88_TXQ_BK] = { RTW88_TX_RING_BK, 0x038a, 0x03ac,
	    RTW88_IMR_BKDOK },
	[RTW88_TXQ_VI] = { RTW88_TX_RING_VI, 0x0386, 0x03a4,
	    RTW88_IMR_VIDOK },
	[RTW88_TXQ_VO] = { RTW88_TX_RING_VO, 0x0384, 0x03a0,
	    RTW88_IMR_VODOK },
};
#define RTW88_BD_OWN		0x8000
#define RTW88_BCN_VALID		0x8000

struct rtw88_pci_poll32 { uint32_t reg, mask, value; };

static int rtw88_pci_resources_detach(struct rtw88_softc *);
static int rtw88_pci_tx_reclaim(struct rtw88_softc *, unsigned);

static int
rtw88_pci_poll(struct rtw88_softc *sc, uint32_t gen, unsigned budget_us,
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

static int
rtw88_pci_ready32(struct rtw88_softc *sc, void *arg)
{
	struct rtw88_pci_poll32 *p = arg;
	uint32_t v;
	int error;

	error = sc->sc_ops->read_4(sc->sc_bus, p->reg, &v);
	if (error != 0)
		return -error;
	return ((v & p->mask) == p->value) ? 0 : 1;
}

static int
rtw88_pci_ready8(struct rtw88_softc *sc, void *arg)
{
	struct rtw88_pci_poll32 *p = arg;
	uint8_t v;
	int error;

	error = sc->sc_ops->read_1(sc->sc_bus, p->reg, &v);
	if (error != 0)
		return -error;
	return ((v & (uint8_t)p->mask) == (uint8_t)p->value) ? 0 : 1;
}

static void
rtw88_pci_put16(uint8_t *p, uint16_t v)
{

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void
rtw88_pci_put32(uint8_t *p, uint32_t v)
{

	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static int
rtw88_pci_rmw8(struct rtw88_softc *sc, uint32_t addr, uint8_t clear, uint8_t set)
{
	uint8_t v;
	int error;

	error = sc->sc_ops->read_1(sc->sc_bus, addr, &v);
	if (error != 0)
		return error;
	v = (uint8_t)((v & ~clear) | set);
	return sc->sc_ops->write_1(sc->sc_bus, addr, v);
}

static int
rtw88_pci_post_fw_chunk(struct rtw88_softc *sc, uint32_t gen, size_t len)
{
	struct rtw88_tx_info tx;
	struct rtw88_dma *bcn = &sc->sc_ring[RTW88_RING_BCN];
	struct rtw88_pci_poll32 valid;
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
	error = rtw88_chip_tx_desc(sc, sc->sc_fw_dma.vaddr,
	    sc->sc_fw_dma.size, &tx);
	if (error != 0)
		return error;
	bd = bcn->vaddr;
	memset(bd, 0, RTW8822C_TX_BUFFER_DESC_SIZE);
	total = (uint32_t)len + RTW88_TX_DESC;
	psb = (total - 1) / 128 + 1;
	psb |= RTW88_BD_OWN;
	dma = (uint32_t)sc->sc_fw_dma.addr;
	rtw88_pci_put16(bd, RTW88_TX_DESC);
	rtw88_pci_put16(bd + 2, (uint16_t)psb);
	rtw88_pci_put32(bd + 4, dma);
	rtw88_pci_put16(bd + 8, (uint16_t)len);
	rtw88_pci_put32(bd + 12, dma + RTW88_TX_DESC);
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
	error = rtw88_pci_rmw8(sc, RTW88_REG_CR + 1, 0, 0x01);
	if (error != 0)
		return error;
	error = rtw88_pci_rmw8(sc, RTW88_REG_BCN_CTRL, 0x08, 0x10);
	if (error != 0)
		return error;
	error = rtw88_pci_rmw8(sc, RTW88_REG_FWHW_TXQ + 2, 0x40, 0);
	if (error != 0)
		return error;
	error = rtw88_pci_rmw8(sc, RTW88_REG_BCN_WORK, 0, RTW88_BCN_WORK_FLAG);
	if (error != 0)
		return error;
	/*
	 * The beacon queue has no completion write-back: the host OWN bit
	 * stays set. The chip reports the copy by setting BCN_VALID.
	 */
	valid.reg = RTW88_REG_FIFOPAGE_2 + 1;
	valid.mask = RTW88_BCN_VALID >> 8;
	valid.value = RTW88_BCN_VALID >> 8;
	error = rtw88_pci_poll(sc, gen, RTW88_COMMAND_US, rtw88_pci_ready8, &valid);
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

static int
rtw88_pci_write_ring(struct rtw88_softc *sc, uint32_t desa, uint32_t num,
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

static void
rtw88_pci_rx_arm(struct rtw88_softc *sc, unsigned idx)
{
	uint8_t *bd = (uint8_t *)sc->sc_ring[RTW88_RING_RX].vaddr +
	    RTW8822C_RX_BUFFER_DESC_SIZE * idx;

	rtw88_pci_put16(bd, RTW88_RX_BUF_SIZE);
	rtw88_pci_put16(bd + 2, 0);
	rtw88_pci_put32(bd + 4, (uint32_t)sc->sc_rxbuf[idx].addr);
	sc->sc_ops->dma_sync(sc->sc_bus, &sc->sc_rxbuf[idx],
	    BUS_DMASYNC_PREREAD);
}

static int
rtw88_pci_hci_setup(struct rtw88_softc *sc)
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
	error = rtw88_pci_write_ring(sc, RTW88_REG_TXBD_DESA_BCN, 0, 0,
	    RTW88_RING_BCN, 16);
	if (error == 0)
		error = rtw88_pci_write_ring(sc, 0x1320, 0x1328, 1, 7, 16);
	if (error == 0)
		error = rtw88_pci_write_ring(sc, 0x0330, 0x038a, 1, 0, 16);
	if (error == 0)
		error = rtw88_pci_write_ring(sc, 0x0328, 0x0388, 1, 1, 16);
	if (error == 0)
		error = rtw88_pci_write_ring(sc, 0x0318, 0x0384, 1, 3, 16);
	if (error == 0)
		error = rtw88_pci_write_ring(sc, 0x0320, 0x0386, 1, 2, 16);
	if (error == 0)
		error = rtw88_pci_write_ring(sc, 0x0310, 0x0380, 1, 5, 16);
	if (error == 0)
		error = rtw88_pci_write_ring(sc, 0x0340, 0x038c, 1, 6, 16);
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
		rtw88_pci_rx_arm(sc, i);
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
rtw88_pci_iddma_chunk(struct rtw88_softc *sc, uint32_t gen, const uint8_t *data,
    size_t len, uint32_t dst, int first)
{
	struct rtw88_pci_poll32 own;
	uint32_t ctrl;
	int error;

	if (sc->sc_fw_dma.vaddr == NULL ||
	    len + RTW88_TX_DESC > sc->sc_fw_dma.size)
		return EOVERFLOW;
	memcpy((uint8_t *)sc->sc_fw_dma.vaddr + RTW88_TX_DESC, data, len);
	error = rtw88_pci_post_fw_chunk(sc, gen, len);
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
	error = rtw88_pci_poll(sc, gen, RTW88_COMMAND_US, rtw88_pci_ready32, &own);
	if (error != 0)
		printf("rtwce: firmware DDMA timed out\n");
	return error;
}

static int
rtw88_pci_fw_section(struct rtw88_softc *sc, uint32_t gen,
    const uint8_t *fw, size_t fw_len, const struct rtw88_fw_section *section)
{
	size_t off = 0;
	int first = 1, error;
	uint32_t ctrl;

	if (section == NULL || fw == NULL || section->offset > fw_len ||
	    section->size > fw_len - section->offset ||
	    section->size > UINT32_MAX - section->load_address)
		return EINVAL;
	if (sc->sc_stop_requested || sc->sc_generation != gen)
		return EINTR;
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
		error = rtw88_pci_iddma_chunk(sc, gen, fw + section->offset + off,
		    chunk, section->load_address + (uint32_t)off, first);
		if (error != 0)
			return error;
		first = 0;
		off += chunk;
	}
	error = sc->sc_ops->read_4(sc->sc_bus, RTW88_REG_DDMA_CTRL, &ctrl);
	if (error != 0)
		return error;
	return (ctrl & RTW88_DDMA_CHKSUM_STS) != 0 ? EBADMSG : 0;
}

static int
rtw88_pci_hci_start(struct rtw88_softc *sc)
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

static void
rtw88_pci_rx(struct rtw88_softc *sc)
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
			{
			struct rtw88_dma *buf = &sc->sc_rxbuf[rp];

			sc->sc_ops->dma_sync(sc->sc_bus, buf,
			    BUS_DMASYNC_POSTREAD);
			rtw88_rx_deliver(sc, buf->vaddr, RTW88_RX_BUF_SIZE,
			    sequence);
		}
		sc->sc_rx_tag = (sc->sc_rx_tag + 1) % RTW88_RX_TAG_MAX;
		rtw88_pci_rx_arm(sc, rp);
		rp = (rp + 1) % sc->sc_nrxbuf;
	}
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
	sc->sc_rx_rp = rp;
	(void)sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_RXBD_IDX, (uint16_t)rp);
}

static int
rtw88_pci_rx_frontier(struct rtw88_softc *sc, uint32_t expected_generation,
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

static int
rtw88_pci_tx_reclaim(struct rtw88_softc *sc, unsigned q)
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

static int
rtw88_pci_tx_drain_snapshot(struct rtw88_softc *sc, unsigned *pending)
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
		error = rtw88_pci_tx_reclaim(sc, q);
		if (error != 0)
			return error;
		*pending += txq->queued;
	}
	return 0;
}

static int
rtw88_pci_hci_intr(struct rtw88_softc *sc)
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
			(void)rtw88_pci_tx_reclaim(sc, q);
	if ((s0 & RTW88_IMR_ROK) != 0)
		rtw88_pci_rx(sc);
	return 1;
}

static int
rtw88_pci_resources_attach(struct rtw88_softc *sc)
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

static int
rtw88_pci_resources_detach(struct rtw88_softc *sc)
{
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

static unsigned
rtw88_pci_space(struct rtw88_softc *sc, unsigned q)
{
	const struct rtw88_txq *txq;

	if (q >= RTW88_NTXQ)
		return 0;
	txq = &sc->sc_txq[q];
	return txq->rp > txq->wp ? txq->rp - txq->wp - 1 :
	    RTW88_TX_SLOTS - txq->wp + txq->rp - 1;
}

static int
rtw88_pci_xmit(struct rtw88_softc *sc, unsigned q, const uint8_t *frame,
    size_t len, const struct rtw88_tx_info *tx, enum rtw88_tx_progress *progress)
{
	struct rtw88_txq *txq;
	struct rtw88_dma *ring, *buf;
	uint8_t *bd;
	uint32_t addr;
	unsigned avail;
	int error;

	if (progress == NULL)
		return EINVAL;
	*progress = RTW88_TX_NOT_STARTED;
	if (q >= RTW88_NTXQ || frame == NULL || tx == NULL ||
	    len > RTW88_TX_BUF_SIZE - RTW8822C_TX_PACKET_DESC_SIZE ||
	    tx->packet_size != len)
		return EINVAL;
	txq = &sc->sc_txq[q];
	ring = &sc->sc_ring[rtw88_txq_hw[q].ring];
	if (txq->nbuf != RTW88_TX_SLOTS || ring->cookie == NULL ||
	    ring->vaddr == NULL ||
	    ring->size < RTW88_TX_SLOTS * RTW8822C_TX_BUFFER_DESC_SIZE)
		return ENXIO;
	if (txq->wp >= RTW88_TX_SLOTS || txq->rp >= RTW88_TX_SLOTS)
		return EPROTO;
	/* pci.h avail_desc: one slot stays empty to tell full from empty. */
	avail = rtw88_pci_space(sc, q);
	if (avail == 0) {
		sc->sc_stats.tx_full++;
		return ENOBUFS;
	}
	buf = &txq->buf[txq->wp];
	if (buf->cookie == NULL || buf->vaddr == NULL ||
	    buf->size < RTW8822C_TX_PACKET_DESC_SIZE + len)
		return ENXIO;
	error = rtw88_chip_tx_desc(sc, buf->vaddr, buf->size, tx);
	if (error != 0)
		return error;
	memcpy((uint8_t *)buf->vaddr + RTW8822C_TX_PACKET_DESC_SIZE, frame,
	    len);
	/* DMA ownership can become uncertain at the first publication. */
	*progress = RTW88_TX_MAY_HAVE_WRITTEN;
	sc->sc_ops->dma_sync(sc->sc_bus, buf, BUS_DMASYNC_PREWRITE);
	ring = &sc->sc_ring[rtw88_txq_hw[q].ring];
	bd = (uint8_t *)ring->vaddr + RTW8822C_TX_BUFFER_DESC_SIZE * txq->wp;
	addr = (uint32_t)buf->addr;
	memset(bd, 0, RTW8822C_TX_BUFFER_DESC_SIZE);
	rtw88_pci_put16(bd, RTW8822C_TX_PACKET_DESC_SIZE);
	rtw88_pci_put16(bd + 2, (uint16_t)((len + RTW8822C_TX_PACKET_DESC_SIZE -
	    1) / 128 + 1));
	rtw88_pci_put32(bd + 4, addr);
	rtw88_pci_put16(bd + 8, (uint16_t)len);
	rtw88_pci_put32(bd + 12, addr + RTW8822C_TX_PACKET_DESC_SIZE);
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
	txq->wp = (txq->wp + 1) % RTW88_TX_SLOTS;
	txq->queued++;
	sc->sc_stats.tx_queued++;
	error = sc->sc_ops->write_2(sc->sc_bus, rtw88_txq_hw[q].idx,
	    (uint16_t)txq->wp);
	if (error == 0) {
		sc->sc_stats.tx_posted[q]++;
		*progress = RTW88_TX_ACCEPTED;
	}
	return error;
}

static int
rtw88_pci_prepare(struct rtw88_softc *sc)
{
	int error;

	if (sc->sc_ops->bus_master == NULL)
		return ENODEV;
	error = sc->sc_ops->dma_alloc(sc->sc_bus, &sc->sc_fw_dma,
	    RTW88_FW_PAGE_SIZE);
	if (error != 0)
		return error;
	sc->sc_fw_dma_owned = 1;
	error = rtw88_dma_validate(sc->sc_fw_dma.addr, sc->sc_fw_dma.size,
	    RTW8822C_DMA_ADDR_BITS);
	if (error != 0)
		return error;
	/* An enable method may fail after hardware has accepted the write. */
	sc->sc_dma_master = 1;
	error = sc->sc_ops->bus_master(sc->sc_bus, 1);
	if (error != 0)
		return error;
	return 0;
}

static int
rtw88_pci_quiesce(struct rtw88_softc *sc)
{
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
	return 0;
}

static int
rtw88_pci_stop(struct rtw88_softc *sc)
{
	int error;

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
	return sc->sc_dma_error;
}

const struct rtw88_hci_ops rtw88_pci_hci_ops = {
	.attach = rtw88_pci_resources_attach,
	.detach = rtw88_pci_resources_detach,
	.prepare = rtw88_pci_prepare,
	.setup = rtw88_pci_hci_setup,
	.start = rtw88_pci_hci_start,
	.quiesce = rtw88_pci_quiesce,
	.stop = rtw88_pci_stop,
	.intr = rtw88_pci_hci_intr,
	.space = rtw88_pci_space,
	.drain = rtw88_pci_tx_drain_snapshot,
	.frontier = rtw88_pci_rx_frontier,
	.fw_section = rtw88_pci_fw_section,
	.xmit = rtw88_pci_xmit,
	.fw_packet = rtw88_pci_fw_packet,
	.irq_can_run_inline = true,
};

