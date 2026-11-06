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
#include <dev/ic/rtw88_wcpu.h>
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw88_internal.h>
#ifdef RTW88_RFK_EXPERIMENT
#include <dev/ic/rtw8822c_rfk.h>
#endif

#define RTW88_FW_DRV		"rtwce"
#define RTW88_FW_IMG		"rtw8822c_fw.bin"
#define RTW88_REG_CR		0x0100
#define RTW88_REG_PCI_CTRL	0x0300
#define RTW88_REG_HIMR0		0x00b0
#define RTW88_REG_HISR0		0x00b4
#define RTW88_REG_HIMR3		0x10b8
#define RTW88_REG_HISR3		0x10bc
#define RTW88_REG_RXBD_IDX	0x03b4
#define RTW88_REG_RCR		0x0608
#define RTW88_RCR		UINT32_C(0xf410400e)
#define RTW88_LBMODE_IMMEDIATE	0x0b

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
	error = rtw88_fw_parse(fw, fw_len, &rtw8822c_fw_profile, &info);
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
	error = rtw88_wcpu_hw_feature_request(sc, gen);
	if (error != 0)
		goto fail;
	stage = "firmware download";
	error = rtw88_wcpu_fw_download(sc, gen, fw, fw_len, &info);
	if (error != 0)
		goto fail;
	stage = "HW feature report";
	error = rtw88_wcpu_hw_feature_read(sc, gen);
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
	.station = rtw88_wcpu_station,
	.edca = rtw88_wcpu_edca,
	.ra = rtw88_wcpu_ra,
	.mailbox_begin = rtw88_wcpu_mailbox_begin,
	.mailbox_end = rtw88_wcpu_mailbox_end,
	.command = rtw88_wcpu_command,
	.diag = rtw8822c_runtime_diag,
	.loopback = rtw8822c_runtime_loopback,
#ifdef RTW88_VHT80
	.tuple_check = rtw8822c_runtime_tuple_check,
	.tx_primary_sc = rtw8822c_runtime_tx_primary_sc,
#endif
	.tx_desc = rtw8822c_runtime_tx_desc,
	.rx_desc = rtw8822c_runtime_rx_desc,
};
