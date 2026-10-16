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

void
rtw88_stop(struct rtw88_softc *sc)
{

	sc->sc_stop_requested = 1;
	sc->sc_generation++;
	if (sc->sc_running) {
		/* No new handler work, masks off, then wait out a running one. */
		sc->sc_running = 0;
		(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR0, 0);
		(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR1, 0);
		(void)sc->sc_ops->write_4(sc->sc_bus, RTW88_REG_HIMR3, 0);
		if (sc->sc_ops->intr_barrier != NULL)
			sc->sc_ops->intr_barrier(sc->sc_bus);
	}
	sc->sc_started = 0;
	sc->sc_rf_enabled = 0;
	sc->sc_bootstrapping = 0;
	if (sc->sc_powered) {
		(void)rtw8822c_power(sc, 0);
		sc->sc_powered = 0;
	}
	if (sc->sc_dma_master && sc->sc_ops->bus_master != NULL) {
		static const uint32_t desa[] = {
			0x0330, 0x0328, 0x0320, 0x0318, RTW88_REG_TXBD_DESA_BCN,
			0x0310, 0x0340, 0x1320, 0x0338
		};
		unsigned i;

		for (i = 0; i < __arraycount(desa); i++)
			(void)sc->sc_ops->write_4(sc->sc_bus, desa[i], 0);
		(void)sc->sc_ops->bus_master(sc->sc_bus, 0);
		sc->sc_dma_master = 0;
	}
	if (sc->sc_fw_dma_owned) {
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_fw_dma);
		sc->sc_fw_dma_owned = 0;
	}
}

int
rtw88_start(struct rtw88_softc *sc)
{
	const uint8_t *fw = NULL;
	size_t fw_len = 0;
	struct rtw88_fw_info info;
	uint32_t gen;
	int error;

	if (!sc->sc_resources_ready)
		return ENXIO;
	if (sc->sc_started)
		return EBUSY;
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
	error = sc->sc_ops->dma_alloc(sc->sc_bus, &sc->sc_fw_dma,
	    RTW88_FW_PAGE_SIZE);
	if (error != 0)
		goto fail;
	sc->sc_fw_dma_owned = 1;
	error = rtw88_dma_validate(sc->sc_fw_dma.addr, sc->sc_fw_dma.size,
	    RTW8822C_DMA_ADDR_BITS);
	if (error != 0)
		goto fail;
	error = sc->sc_ops->bus_master(sc->sc_bus, 1);
	if (error != 0)
		goto fail;
	sc->sc_dma_master = 1;
	error = rtw8822c_power(sc, 1);
	if (error != 0) {
		printf("rtwce: power on failed: %d\n", error);
		goto fail;
	}
	sc->sc_powered = 1;
	error = rtw88_fw_download(sc, gen, fw, &info);
	if (error != 0)
		goto fail;
	/* Reference rtw_power_on: reset rings, MAC, PHY, then HCI start. */
	error = rtw88_hci_setup(sc);
	if (error != 0)
		goto fail;
	error = rtw8822c_mac_init(sc);
	if (error != 0) {
		printf("rtwce: MAC init failed: %d\n", error);
		goto fail;
	}
	error = rtw8822c_load_tables(sc);
	if (error != 0)
		goto fail;
	if (sc->sc_stop_requested || sc->sc_generation != gen) {
		error = EINTR;
		goto fail;
	}
	error = rtw88_hci_start(sc);
	if (error != 0)
		goto fail;
	sc->sc_rf_enabled = 1;
	sc->sc_started = 1;
	sc->sc_bootstrapping = 0;
	rtw88_release_fw(sc);
	return 0;
fail:
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
	if (rx.packet_size < RTW88_MIN_FRAME) {
		sc->sc_stats.rx_short++;
		return;
	}
	/* Task 7 hands validated frames to net80211. */
	sc->sc_stats.rx_frames++;
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
		sc->sc_rx_tag = (sc->sc_rx_tag + 1) % RTW88_RX_TAG_MAX;
		rtw88_rx_one(sc, rp);
		rtw88_rx_arm(sc, rp);
		rp = (rp + 1) % sc->sc_nrxbuf;
	}
	sc->sc_ops->dma_sync(sc->sc_bus, ring, BUS_DMASYNC_PREWRITE);
	sc->sc_rx_rp = rp;
	(void)sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_RXBD_IDX, (uint16_t)rp);
}

int
rtw88_diag(struct rtw88_softc *sc, uint32_t *r)
{
	static const uint32_t regs[RTW88_DIAG_REGS] = {
		RTW88_REG_HISR0, RTW88_REG_HIMR0, RTW88_REG_HISR3,
		RTW88_REG_HIMR3, RTW88_REG_RXBD_IDX, RTW88_REG_PCI_CTRL
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
	error = sc->sc_ops->intr_establish(sc->sc_bus);
	if (error != 0)
		goto fail;
	sc->sc_intr_established = 1;
	sc->sc_resources_ready = 1;
	return 0;
fail:
	rtw88_core_detach(sc);
	return error;
}

void
rtw88_core_detach(struct rtw88_softc *sc)
{

	rtw88_stop(sc);
	sc->sc_resources_ready = 0;
	if (sc->sc_intr_established) {
		sc->sc_ops->intr_disestablish(sc->sc_bus);
		sc->sc_intr_established = 0;
	}
	while (sc->sc_nrxbuf != 0) {
		sc->sc_nrxbuf--;
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_rxbuf[sc->sc_nrxbuf]);
	}
	while (sc->sc_nring != 0) {
		sc->sc_nring--;
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_ring[sc->sc_nring]);
	}
}
