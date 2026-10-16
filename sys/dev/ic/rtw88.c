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
#define RTW88_DDMA_LEN_MASK	0x3ffff
#define RTW88_OCP_TXBUF		UINT32_C(0x18780000)
#define RTW88_TX_DESC		48
#define RTW88_FW_CHUNK		0x1000
#define RTW88_REG_HMETFR	0x01cc
#define RTW88_REG_HMEBOX0	0x01d0
#define RTW88_REG_HMEBOX0_EX	0x01f0

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
	memset(sc->sc_fw_dma.vaddr, 0, RTW88_TX_DESC);
	memcpy((uint8_t *)sc->sc_fw_dma.vaddr + RTW88_TX_DESC, data, len);
	sc->sc_ops->dma_sync(sc->sc_bus, &sc->sc_fw_dma, BUS_DMASYNC_PREWRITE);
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
	return rtw88_poll(sc, gen, RTW88_COMMAND_US, rtw88_ready32, &own);
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
	return 0;
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
	error = sc->sc_ops->read_2(sc->sc_bus, RTW88_REG_MCUFW_CTRL, &ctrl);
	if (error != 0)
		return error;
	ctrl = (uint16_t)((ctrl | RTW88_FW_DW_RDY) & ~RTW88_MCUFWDL_EN);
	error = sc->sc_ops->write_2(sc->sc_bus, RTW88_REG_MCUFW_CTRL, ctrl);
	if (error != 0)
		return error;
	ready.reg = RTW88_REG_MCUFW_CTRL;
	ready.mask = 0xffff;
	ready.value = RTW88_FW_READY;
	/*
	 * The mock and silicon report a ready constellation in the low 16
	 * bits. Accept any value that includes the documented ready bits.
	 */
	ready.mask = RTW88_FW_READY;
	ready.value = RTW88_FW_READY;
	return rtw88_poll(sc, gen, RTW88_BOOTSTRAP_US, rtw88_ready32, &ready);
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
	sc->sc_started = 0;
	sc->sc_rf_enabled = 0;
	sc->sc_bootstrapping = 0;
	if (sc->sc_powered) {
		(void)rtw8822c_power(sc, 0);
		sc->sc_powered = 0;
	}
	if (sc->sc_dma_master && sc->sc_ops->bus_master != NULL) {
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
	if (error != 0)
		goto fail;
	sc->sc_powered = 1;
	error = rtw88_fw_download(sc, gen, fw, &info);
	if (error != 0)
		goto fail;
	error = rtw8822c_load_tables(sc);
	if (error != 0)
		goto fail;
	if (sc->sc_stop_requested || sc->sc_generation != gen) {
		error = EINTR;
		goto fail;
	}
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
	while (sc->sc_nring != 0) {
		sc->sc_nring--;
		sc->sc_ops->dma_free(sc->sc_bus, &sc->sc_ring[sc->sc_nring]);
	}
}
