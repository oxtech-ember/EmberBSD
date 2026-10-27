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
#include <dev/ic/rtw88_internal.h>
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
#define RTW88_OCP_DMEM		UINT32_C(0x00200000)
#define RTW88_IMEM_DW_OK	0x08
#define RTW88_IMEM_CHKSUM_OK	0x10
#define RTW88_DMEM_DW_OK	0x20
#define RTW88_DMEM_CHKSUM_OK	0x40
#define RTW88_REG_TXDMA_STATUS	0x0210
#define RTW88_PAGE_OVF		0x04
#define RTW88_REG_FW_DBG7	0x10fc
#define RTW88_REG_HMETFR	0x01cc
#define RTW88_REG_HMEBOX0	0x01d0
#define RTW88_REG_HMEBOX0_EX	0x01f0
#define RTW88_REG_SYS_FUNC_EN	0x0002
#define RTW88_REG_SYS_CLK	0x0008
#define RTW88_REG_RSV_CTRL	0x001c
#define RTW88_REG_CR		0x0100
#define RTW88_REG_TXDMA_PQ	0x010c
#define RTW88_REG_RQPN_CTRL2	0x022c
#define RTW88_REG_FIFOPAGE_1	0x0230
#define RTW88_REG_PCI_CTRL	0x0300
#define RTW88_REG_BCN_CTRL	0x0550
#define RTW88_REG_CPU_DMEM	0x1080
#define RTW88_REG_H2CQ_CSR	0x1330
#define RTW88_REG_HIMR0		0x00b0
#define RTW88_REG_HISR0		0x00b4
#define RTW88_REG_HIMR3		0x10b8
#define RTW88_REG_HISR3		0x10bc
#define RTW88_REG_RXBD_IDX	0x03b4
#define RTW88_REG_RCR		0x0608
#define RTW88_RCR		UINT32_C(0xf410400e)
#define RTW88_RATEID_G		7
#define RTW88_RATEID_B_20M	8
#define RTW88_LBMODE_IMMEDIATE	0x0b
#define RTW88_REG_C2HEVT 0x01a0

struct rtw88_poll32 { uint32_t reg, mask, value; };

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
	return sc->sc_hci->setup(sc);
}

static int
rtw88_section_checksum(struct rtw88_softc *sc, uint32_t addr, int bad_checksum)
{
	uint8_t fw, dw, ok;
	int error;

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
	if (bad_checksum) {
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
    const uint8_t *fw, size_t fw_len, const struct rtw88_fw_section *section)
{
	int error;

	error = sc->sc_hci->fw_section(sc, gen, fw, fw_len, section);
	if (error != 0 && error != EBADMSG)
		return error;
	return rtw88_section_checksum(sc, section->load_address,
	    error == EBADMSG);
}

static int
rtw88_fw_download(struct rtw88_softc *sc, uint32_t gen, const uint8_t *fw,
    size_t fw_len, const struct rtw88_fw_info *info)
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
	error = rtw88_download_section(sc, gen, fw, fw_len, &info->dmem);
	if (error != 0)
		return error;
	error = rtw88_download_section(sc, gen, fw, fw_len, &info->imem);
	if (error != 0)
		return error;
	if (info->emem.size != 0) {
		error = rtw88_download_section(sc, gen, fw, fw_len, &info->emem);
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



static int
rtw8822c_runtime_start(struct rtw88_softc *sc,
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
	    sc->sc_dma_error || sc->sc_hci_error) {
		printf("rtwce: startup blocked by incomplete shutdown: power %d DMA %d HCI %d\n",
		    sc->sc_power_error, sc->sc_dma_error, sc->sc_hci_error);
		return EBUSY;
	}
	if (sc->sc_ops->firmware_load == NULL)
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
	error = sc->sc_chip->identify(sc);
	if (error != 0)
		goto fail;
	stage = "HCI prepare";
	error = sc->sc_hci->prepare(sc);
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
	error = rtw88_fw_download(sc, gen, fw, fw_len, &info);
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
	stage = "HCI setup";
	error = sc->sc_hci->setup(sc);
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
		error = rtw8822c_set_tx_power_tuple(sc, &efuse, tuple, RTW88_POWER_REGD);
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
	error = sc->sc_hci->start(sc);
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

static int
rtw8822c_runtime_channel(struct rtw88_softc *sc,
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
	/*
	 * Keep host-ring frames off a DFS channel. This covers host ring
	 * ownership only; frames held in the MAC need rtw88_tx_quiet().
	 */
	if (rtw88_channel_dfs(ch)) {
		for (unsigned q = 0; q < RTW88_NTXQ; q++)
			if (sc->sc_txq[q].queued != 0)
				return EBUSY;
	}
	error = rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &efuse);
	if (error != 0)
		return error;
	sc->sc_phy.channel_valid = 0;
	rtw88_dfs_close(sc);
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
		error = rtw8822c_set_tx_power_tuple(sc, &efuse, tuple, RTW88_POWER_REGD);
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

static int
rtw8822c_runtime_station(struct rtw88_softc *sc, const uint8_t *bssid,
    unsigned aid, unsigned basic_rates, unsigned slot, int linked)
{
	struct rtw88_io io;
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
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	if (io.error != 0)
		return io.error;
	rtw88_ra_invalidate(sc);
	sc->sc_linked = 0;
	/* Keep data paused and port unlinked until all setup writes succeed. */
	rtw88_io_wr_mask(&io, 0x0100, 0x30000, 0);
	rtw88_io_set(&io, 0x0522, 1, 0x0f);
	rtw88_io_wr(&io, 0x06a4, 2, 0);
	for (i = 0; i < 6; i++) {
		rtw88_io_wr(&io, 0x0610 + i, 1, sc->sc_mac[i]);
		rtw88_io_wr(&io, 0x0618 + i, 1, linked ? bssid[i] : 0);
	}
	/* SECCFG.TXDEC/RXDEC are off: the stack owns CCMP/PN/MIC. */
	rtw88_io_clr(&io, 0x0680, 1, 0x0c);
	rtw88_io_wr_mask(&io, 0x06a8, 0x7ff, aid);
	rtw88_io_wr_mask(&io, 0x0440, 0xfff, basic_rates);
	rtw88_io_wr(&io, 0x051b, 1, slot);
	rtw88_io_set(&io, 0x0550, 1, 0x18);
	rtw88_io_clr(&io, 0x0608, 4, 0xc0);
	if (io.error != 0)
		return io.error;
	media[0] = linked ? 1 : 0;
	io.error = rtw88_fw_command(sc, 0x01, media, sizeof(media));
	if (linked && io.error == 0)
		io.error = rtw88_fw_command(sc, 0x2c, port, sizeof(port));
	if (linked) {
		rtw88_io_set(&io, 0x0608, 4, 0xc0);
		rtw88_io_clr(&io, 0x0550, 1, 0x10);
		rtw88_io_wr(&io, 0x06a4, 2, 0xffff);
		rtw88_io_wr_mask(&io, 0x0100, 0x30000, 2);
		rtw88_io_clr(&io, 0x0522, 1, 0x0f);
	}
	if (io.error == 0) {
		sc->sc_basic_rates = basic_rates;
		sc->sc_linked = linked;
	}
	return io.error;
}

static int
rtw8822c_runtime_edca(struct rtw88_softc *sc,
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

static int
rtw8822c_runtime_ra(struct rtw88_softc *sc, const struct rtw88_ra_config *cfg)
{
	struct rtw88_ra_state ra = { 0 };
	uint8_t payload[7] = { 0 };
	unsigned mask;
	int error, tuple_ok;

	if (cfg == NULL || cfg->epoch == 0 || cfg->use_protection > 1 ||
	    cfg->channel == 0 || cfg->channel != sc->sc_channel ||
	    cfg->common_rates == 0 || (cfg->common_rates & ~0xfffU) != 0 ||
	    cfg->sgi > 1 || (cfg->sgi && cfg->phy == RTW88_RA_LEGACY) ||
	    cfg->ldpc > 1 || (cfg->ldpc && cfg->phy == RTW88_RA_LEGACY))
		return EINVAL;
#ifndef RTW88_VHT80
	if (cfg->sgi || cfg->ldpc)
		return EOPNOTSUPP;
#endif
	if (cfg->phy == RTW88_RA_HT20_1SS) {
		if (cfg->mode != RTW88_FIXED_11A || cfg->channel <= 14 ||
		    cfg->use_protection != 0 || cfg->vht_tx_map != 0 ||
		    rtw88_ht20_policy_check(&cfg->ht, cfg->ht_operation) != 0 ||
		    cfg->profile_generation == 0 || cfg->ht_mcs == 0 ||
		    (cfg->ht_mcs & ~0xffU) != 0)
			return EOPNOTSUPP;
#ifdef RTW88_VHT80
	} else if (cfg->phy == RTW88_RA_VHT_1SS) {
		/* No HT-mixed entry; keep the mandatory 6 Mbps fallback. */
		if (cfg->mode != RTW88_FIXED_11A || cfg->channel <= 14 ||
		    cfg->use_protection != 0 ||
		    rtw88_ht20_policy_check(&cfg->ht, cfg->ht_operation) != 0 ||
		    cfg->profile_generation == 0 || cfg->ht_mcs != 0 ||
		    rtw88_vht_rates(sc, cfg->vht_tx_map, NULL) != 0 ||
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
	if (cfg->phy == RTW88_RA_VHT_1SS)
		tuple_ok = rtw88_vht_committed(sc);
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
	ra.sgi = cfg->sgi;
	ra.ldpc = cfg->ldpc;
	ra.tuple = sc->sc_phy.channel;
	if (cfg->phy == RTW88_RA_HT20_1SS) {
		ra.rate_id = 5; /* Pinned RTW_RATEID_GN_N1SS, 5 GHz only. */
		mask = (unsigned)cfg->ht_mcs << 12 | (mask & 0x30);
#ifdef RTW88_VHT80
	} else if (cfg->phy == RTW88_RA_VHT_1SS) {
		struct rtw88_vht_rate_set rates;

		(void)rtw88_vht_rates(sc, cfg->vht_tx_map, &rates);
		ra.rate_id = rates.rate_id;
		ra.vht_seed = rates.seed;
		mask = rates.mask | (mask & 0x10);
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
	/* Initial RA level1; bit7 SGI_EN lets firmware RA choose short GI. */
	payload[1] = ra.rate_id | (1U << 5) | (unsigned)ra.sgi << 7;
	/* DIS_PT, BW20, NO_UPDATE0, VHT0; bit2 LDPC. */
	payload[2] = 0x40 | (unsigned)ra.ldpc << 2;
	if (ra.phy == RTW88_RA_VHT_1SS)
		/* VHT1 and the committed width: BW40 1, BW80 2. */
		payload[2] |= 0x10 | (ra.tuple.width_mhz == 80 ? 2 : 1);
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
rtw8822c_runtime_mailbox_begin(struct rtw88_softc *sc, unsigned count, int *ready)
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

static int
rtw8822c_runtime_mailbox_end(struct rtw88_softc *sc, int error)
{

	if (sc->sc_mailbox.active && error == 0 &&
	    (sc->sc_mailbox.generation != sc->sc_generation ||
	    sc->sc_mailbox.sent != sc->sc_mailbox.total))
		error = EPROTO;
	memset(&sc->sc_mailbox, 0, sizeof(sc->sc_mailbox));
	return error;
}

static int
rtw8822c_runtime_command(struct rtw88_softc *sc, uint8_t id, const void *payload,
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

static int
rtw8822c_runtime_diag(struct rtw88_softc *sc, uint32_t *r)
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

static int
rtw8822c_runtime_loopback(struct rtw88_softc *sc, int mode)
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

static int
rtw8822c_runtime_stop(struct rtw88_softc *sc)
{
	return rtw8822c_power(sc, 0);
}

static int
rtw8822c_runtime_tx_desc(struct rtw88_softc *sc, uint8_t *out, size_t size,
    const struct rtw88_tx_info *info)
{
	(void)sc;
	return rtw88_tx_encode(out, size, info);
}

static int
rtw8822c_runtime_rx_desc(struct rtw88_softc *sc, const uint8_t *data,
    size_t size, struct rtw88_rx_info *info)
{
	int error;

	error = rtw88_rx_decode(data, size, info);
	if (error != 0)
		return error;
	if ((info->flags & (RTW88_RX_C2H | RTW88_RX_CRC_ERROR |
	    RTW88_RX_ICV_ERROR)) == 0)
		(void)rtw88_rx_signal_decode_tuple(data + info->driver_info_offset,
		    info->driver_info_size, info, &sc->sc_phy, sc->sc_generation,
		    &sc->sc_phy.channel, &info->signal);
	return 0;
}

static int
rtw8822c_runtime_fw_init(struct rtw88_softc *sc)
{
	uint8_t packet[32] = { 1, 0xff, 0x0d, 0, 12, 0, 0, 0, [10] = 56 };
	int error;

	if (sc->sc_hci == NULL)
		return EINVAL;
	if (sc->sc_hci->fw_packet == NULL)
		return EOPNOTSUPP;
	if (sc->sc_packet_h2c.busy || sc->sc_packet_h2c.dirty ||
	    sc->sc_packet_h2c.error)
		return EBUSY;
	if (!sc->sc_packet_h2c.active || !sc->sc_running || !sc->sc_powered)
		return ENXIO;
	if (sc->sc_stop_requested ||
	    sc->sc_generation != sc->sc_packet_h2c.generation)
		return EINTR;
	if (sc->sc_started || sc->sc_packet_h2c.attempted ||
	    sc->sc_packet_h2c.initialized)
		return EBUSY;
	if (!sc->sc_bootstrapping)
		return ENXIO;
	if (sc->sc_rfe > 6 || sc->sc_cut > 6)
		return ENOTSUP;
	sc->sc_packet_h2c.boot_waited = 0;
	error = sc->sc_hci->fw_packet(sc, packet, sizeof(packet));
	if (error == 0) {
		memset(packet, 0, sizeof(packet));
		packet[0] = 1;
		packet[1] = 0xff;
		packet[2] = 0x11;
		packet[4] = 16;
		packet[8] = sc->sc_rfe;
		packet[9] = 2;
		packet[10] = sc->sc_cut;
		packet[11] = 0x33;
		error = sc->sc_hci->fw_packet(sc, packet, sizeof(packet));
	}
	if (error == 0)
		sc->sc_packet_h2c.initialized = 1;
	return error;
}

static int
rtw8822c_runtime_attach_check(struct rtw88_softc *sc)
{
	(void)sc;
#ifdef RTW88_RFK_EXPERIMENT
	if (rtw8822c_rfk_blocked())
		return EBUSY;
#endif
	return 0;
}

static int
rtw8822c_runtime_detach_check(struct rtw88_softc *sc)
{
	(void)sc;
#ifdef RTW88_RFK_EXPERIMENT
	if (rtw8822c_rfk_active(sc))
		return EBUSY;
#endif
	return 0;
}

static int
rtw8822c_runtime_detach(struct rtw88_softc *sc)
{
	(void)sc;
#ifdef RTW88_RFK_EXPERIMENT
	return rtw8822c_rfk_free(sc);
#else
	return 0;
#endif
}

#ifdef RTW88_VHT80
static int
rtw8822c_runtime_tuple_check(const struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple)
{
	struct rtw8822c_efuse efuse;
	int error;

	if (sc == NULL || tuple == NULL)
		return EINVAL;
	if (rtw88_channel_admit(tuple) != 0)
		return EOPNOTSUPP;
	if (!sc->sc_resources_ready)
		return ENXIO;
	error = rtw8822c_efuse_decode(sc->sc_efuse_phys,
	    sizeof(sc->sc_efuse_phys), &efuse);
	if (error != 0)
		return error;
	return rtw8822c_tx_power_check(sc, &efuse, tuple, RTW88_POWER_REGD);
}

static int
rtw8822c_runtime_tx_primary_sc(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple, uint8_t *out)
{
	struct rtw8822c_channel_geometry geometry;
	int error;

	(void)sc;
	if (out == NULL)
		return EINVAL;
	error = rtw8822c_channel_geometry(tuple, &geometry);
	if (error != 0)
		return error;
	*out = (uint8_t)geometry.channel_index;
	return 0;
}
#endif

#ifdef RTW88_RFK_EXPERIMENT
static int
rtw8822c_runtime_calibration_blocked(const struct rtw88_softc *sc)
{
	(void)sc;
	return rtw8822c_rfk_blocked();
}

static const struct rtw88_calibration_ops rtw8822c_calibration_ops = {
	.blocked = rtw8822c_runtime_calibration_blocked,
	.active = rtw8822c_rfk_active,
	.begin = rtw8822c_rfk_begin,
	.validate = rtw8822c_rfk_validate,
	.run = rtw8822c_rfk_run,
	.retire = rtw8822c_rfk_retire,
	.seal = rtw8822c_rfk_seal,
	.progress = rtw8822c_rfk_progress,
};
#endif

const struct rtw88_chip_ops rtw8822c_chip_ops = {
#ifdef RTW88_RFK_EXPERIMENT
	.calibration = &rtw8822c_calibration_ops,
#endif
	.caps = {
		.bands = RTW88_BAND_2G | RTW88_BAND_5G,
#ifndef RTW88_VHT80
		.widths = RTW88_WIDTH_20,
#else
		.widths = RTW88_WIDTH_20 | RTW88_WIDTH_40 | RTW88_WIDTH_80,
#endif
		.nss = 2,
		.tx_desc_size = RTW8822C_TX_PACKET_DESC_SIZE,
		.rx_desc_size = RTW8822C_RX_PACKET_DESC_SIZE,
		.ht_mcs = 0xff,
		.features = RTW88_CAP_WPA2 | RTW88_CAP_WME | RTW88_CAP_RXBA |
		    RTW88_CAP_AMSDU | RTW88_CAP_TX_AMPDU
#ifdef RTW88_VHT80
		    | RTW88_CAP_VHT
#endif
	},
	.attach_check = rtw8822c_runtime_attach_check,
	.detach_check = rtw8822c_runtime_detach_check,
	.detach = rtw8822c_runtime_detach,
	.identify = rtw8822c_identify,
	.start = rtw8822c_runtime_start,
	.stop = rtw8822c_runtime_stop,
	.fw_init = rtw8822c_runtime_fw_init,
	.channel = rtw8822c_runtime_channel,
	.station = rtw8822c_runtime_station,
	.edca = rtw8822c_runtime_edca,
	.ra = rtw8822c_runtime_ra,
	.mailbox_begin = rtw8822c_runtime_mailbox_begin,
	.mailbox_end = rtw8822c_runtime_mailbox_end,
	.command = rtw8822c_runtime_command,
	.diag = rtw8822c_runtime_diag,
	.loopback = rtw8822c_runtime_loopback,
#ifdef RTW88_VHT80
	.tuple_check = rtw8822c_runtime_tuple_check,
	.tx_primary_sc = rtw8822c_runtime_tx_primary_sc,
#endif
	.tx_desc = rtw8822c_runtime_tx_desc,
	.rx_desc = rtw8822c_runtime_rx_desc,
};
