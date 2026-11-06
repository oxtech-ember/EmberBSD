/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Native RTL8821CE lifecycle; hardware sequences live in chip modules. */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#ifdef _KERNEL
#include <sys/kmem.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#endif
#include <dev/ic/rtw88var.h>
#include <dev/ic/rtw88_internal.h>
#include <dev/ic/rtw88_wcpu.h>
#include <dev/ic/rtw8821c.h>
#include <dev/ic/rtw8821c_hw.h>
#include <dev/ic/rtw8821c_boot.h>
#include <dev/ic/rtw8821c_coex.h>
#include <dev/ic/rtw8821c_phy.h>

struct rtw8821c_runtime {
	struct rtw8821c_phy phy;
	uint32_t generation;
	int identity_ready, needs_iqk;
	int (*current)(void *);
	void *current_cookie;
};

static int
rtw8821c_phy_current(void *cookie)
{
	struct rtw8821c_runtime *ctx = cookie;
	struct rtw88_softc *sc = ctx->phy.sc;

	if (sc->sc_stop_requested || sc->sc_generation != ctx->generation)
		return EINTR;
	return ctx->current != NULL ? ctx->current(ctx->current_cookie) : 0;
}

static int
rtw8821c_tuple_check(const struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple)
{
	(void)sc;
	if (tuple == NULL)
		return EINVAL;
	return tuple->width_mhz == 20 && tuple->primary == tuple->center &&
	    tuple->primary >= 1 && tuple->primary <= 13 ? 0 : EOPNOTSUPP;
}

static int
rtw8821c_attach(struct rtw88_softc *sc)
{
	struct rtw8821c_runtime *ctx;

	if (sc->sc_chip_private != NULL)
		return EBUSY;
	ctx = kmem_zalloc(sizeof(*ctx), KM_NOSLEEP);
	if (ctx == NULL)
		return ENOMEM;
	ctx->phy.sc = sc;
	ctx->phy.check = rtw8821c_phy_current;
	ctx->phy.check_cookie = ctx;
	sc->sc_chip_private = ctx;
	return 0;
}

static int
rtw8821c_detach(struct rtw88_softc *sc)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;

	if (sc->sc_started || sc->sc_running || sc->sc_power_dirty ||
	    sc->sc_powered || sc->sc_dma_master)
		return EBUSY;
	if (ctx != NULL) {
		kmem_free(ctx, sizeof(*ctx));
		sc->sc_chip_private = NULL;
	}
	return 0;
}

/* Discover the permanent address before attaching net80211. DMA remains off.
 * A failed or incomplete power-down cannot publish a usable identity. */
static int
rtw8821c_discover(struct rtw88_softc *sc)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	int error, close_error;

	if (ctx == NULL || !sc->sc_resources_ready)
		return ENXIO;
	error = rtw8821c_identify(sc);
	if (error != 0 || ctx->identity_ready)
		return error;
	if (sc->sc_started || sc->sc_running || sc->sc_bootstrapping ||
	    sc->sc_power_dirty || sc->sc_power_error || sc->sc_dma_master)
		return EBUSY;
	sc->sc_bootstrapping = 1;
	sc->sc_power_dirty = 1;
	error = rtw8821c_power_pre(sc);
	if (error == 0)
		error = rtw8821c_power(sc, 1);
	if (error == 0) {
		sc->sc_powered = 1;
		error = rtw8821c_power_post(sc);
	}
	if (error == 0)
		error = rtw8821c_read_efuse(sc);
	close_error = rtw8821c_power(sc, 0);
	sc->sc_power_error = close_error;
	if (close_error == 0)
		sc->sc_power_dirty = sc->sc_powered = 0;
	sc->sc_bootstrapping = 0;
	if (error == 0)
		error = close_error;
	if (error == 0)
		error = rtw8821c_efuse_decode(sc->sc_efuse_phys,
		    sizeof(sc->sc_efuse_phys), &ctx->phy.efuse);
	if (error == 0) {
		ctx->identity_ready = 1;
		printf("rtwce: RTL8821C cut %u RFE %u board %#x BT %#x "
		    "crystal %#x channel plan %#x\n", sc->sc_cut, sc->sc_rfe,
		    ctx->phy.efuse.logical[0xc1], ctx->phy.efuse.logical[0xc3],
		    ctx->phy.efuse.xtal, ctx->phy.efuse.channel_plan);
	}
	return error;
}

static int
rtw8821c_packet(void *cookie, const uint8_t *packet, size_t len)
{
	struct rtw8821c_runtime *ctx = cookie;
	int error;

	if (ctx->current != NULL &&
	    (error = ctx->current(ctx->current_cookie)) != 0)
		return error;
	return rtw88_fw_packet(ctx->phy.sc, packet, len);
}

static int
rtw8821c_iqk_wait(void *cookie, unsigned ms)
{
	struct rtw8821c_runtime *ctx = cookie;
	int error;

	if (ctx->current == NULL || ms != 20)
		return EINVAL;
	error = ctx->current(ctx->current_cookie);
	if (error != 0)
		return error;
	/* The state worker pins lifetime, owns hardware mutation and holds no
	 * spin lock here. Sleeping releases the CPU during firmware IQK. */
	(void)kpause("rtwiqk", false, MAX(1, mstohz(ms)), NULL);
	return ctx->current(ctx->current_cookie);
}

static int
rtw8821c_prepare_station(struct rtw88_softc *sc,
    int (*current)(void *), void *cookie)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	int error;

	if (ctx == NULL || current == NULL)
		return EINVAL;
	if (!sc->sc_started || !sc->sc_running || sc->sc_stop_requested)
		return ENXIO;
	error = current(cookie);
	if (error != 0 || !ctx->needs_iqk)
		return error;
	ctx->current = current;
	ctx->current_cookie = cookie;
	ctx->phy.command = rtw8821c_packet;
	ctx->phy.command_cookie = ctx;
	ctx->phy.wait_ms = rtw8821c_iqk_wait;
	ctx->phy.wait_cookie = ctx;
	ctx->phy.associated = sc->sc_linked != 0;
	error = rtw8821c_calibrate(&ctx->phy);
	if (error == 0)
		error = current(cookie);
	ctx->current = NULL;
	ctx->current_cookie = NULL;
	if (error == 0)
		ctx->needs_iqk = 0;
	return error;
}

static int
rtw8821c_fw_init(struct rtw88_softc *sc)
{
	/* 512 TX pages minus 52 reserved; FW buffer starts at page508.
	 * general_info.boundary = 508 - 460 = 48 (donor mac.c/fw.c). */
	uint8_t packet[32] = {1, 0xff, 0x0d, 0, 12, 0, 0, 0, [10] = 48};
	int error;

	if (sc->sc_hci == NULL || sc->sc_hci->fw_packet == NULL)
		return EOPNOTSUPP;
	if (!sc->sc_bootstrapping || sc->sc_started ||
	    !sc->sc_running || !sc->sc_powered || !sc->sc_packet_h2c.active)
		return ENXIO;
	if (sc->sc_stop_requested ||
	    sc->sc_generation != sc->sc_packet_h2c.generation)
		return EINTR;
	if (sc->sc_packet_h2c.busy || sc->sc_packet_h2c.dirty ||
	    sc->sc_packet_h2c.error || sc->sc_packet_h2c.attempted ||
	    sc->sc_packet_h2c.initialized)
		return EBUSY;
	sc->sc_packet_h2c.boot_waited = 0;
	error = sc->sc_hci->fw_packet(sc, packet, sizeof(packet));
	if (error == 0) {
		memset(packet, 0, sizeof(packet));
		packet[0] = 1;
		packet[1] = 0xff;
		packet[2] = 0x11;
		packet[4] = 16;
		packet[8] = sc->sc_rfe;
		packet[9] = 4; /* FW_RF_1T1R, not the host RF enum. */
		packet[10] = sc->sc_cut;
		packet[11] = 0x11; /* RX/TX antenna A. */
		error = sc->sc_hci->fw_packet(sc, packet, sizeof(packet));
	}
	if (error == 0)
		sc->sc_packet_h2c.initialized = 1;
	return error;
}

static int
rtw8821c_start(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *target)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	struct rtw88_channel_tuple tuple;
	struct rtw88_fw_info info;
	const uint8_t *fw = NULL;
	size_t fw_len = 0;
	uint32_t gen;
	const char *stage;
	int error;

	error = rtw8821c_tuple_check(sc, target);
	if (error != 0)
		return error;
	tuple = *target;
	if (ctx == NULL || !ctx->identity_ready || !sc->sc_resources_ready)
		return ENXIO;
	if (sc->sc_started || sc->sc_bootstrapping || sc->sc_power_dirty ||
	    sc->sc_power_error || sc->sc_dma_master || sc->sc_dma_error ||
	    sc->sc_hci_error)
		return EBUSY;
	if (sc->sc_generation == UINT32_MAX)
		return EOVERFLOW;
	if (sc->sc_ops->firmware_load == NULL)
		return ENODEV;
	gen = sc->sc_generation;
	error = sc->sc_ops->firmware_load(sc->sc_bus, "rtw88",
	    "rtw8821c_fw.bin", &fw, &fw_len);
	if (error != 0)
		return error;
	if (sc->sc_generation != gen) {
		error = EINTR;
		goto release;
	}
	error = rtw88_fw_parse(fw, fw_len, &rtw8821c_fw_profile, &info);
	if (error != 0)
		goto release;
	if (sc->sc_generation != gen) {
		error = EINTR;
		goto release;
	}
	sc->sc_generation++;
	gen = sc->sc_generation;
	sc->sc_stop_requested = 0;
	ctx->generation = gen;
	sc->sc_bootstrapping = 1;
	sc->sc_phy.channel_valid = 0;
	ctx->needs_iqk = 1;
	ctx->phy.post_valid = 0;
	memset(&sc->sc_hw_feature, 0, sizeof(sc->sc_hw_feature));
#define STEP(label, call) do { \
	stage = label; \
	error = (call); \
	if (error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen)) \
		error = EINTR; \
	if (error != 0) goto fail; \
} while (0)
	STEP("identity", rtw8821c_identify(sc));
	STEP("HCI prepare", sc->sc_hci->prepare(sc));
	/* Arm DMA before MAC power-on, as in the donor rtw_power_on(). */
	STEP("HCI setup", sc->sc_hci->setup(sc));
	sc->sc_power_dirty = 1;
	STEP("power preinit", rtw8821c_power_pre(sc));
	STEP("power on", rtw8821c_power(sc, 1));
	sc->sc_powered = 1;
	STEP("power postinit", rtw8821c_power_post(sc));
	STEP("HW feature request", rtw88_wcpu_hw_feature_request(sc, gen));
	STEP("firmware", rtw88_wcpu_fw_download(sc, gen, fw, fw_len, &info));
	STEP("HW feature report", rtw88_wcpu_hw_feature_read(sc, gen));
	sc->sc_h2c_box = 0;
	memset(&sc->sc_packet_h2c, 0, sizeof(sc->sc_packet_h2c));
	sc->sc_packet_h2c.active = 1;
	sc->sc_packet_h2c.generation = gen;
	STEP("HCI reset", sc->sc_hci->setup(sc));
	STEP("MAC", rtw8821c_mac_init(sc));
	STEP("PHY preinit", rtw8821c_phy_pre(&ctx->phy));
	STEP("PHY tables", rtw8821c_load_tables(&ctx->phy));
	STEP("PHY postinit", rtw8821c_phy_post(&ctx->phy));
	STEP("receive filter", sc->sc_ops->write_4(sc->sc_bus, 0x0608,
	    UINT32_C(0xf410400e)));
	STEP("channel", rtw8821c_set_channel_tuple(&ctx->phy, &tuple));
	STEP("TX power", rtw8821c_set_tx_power_tuple(&ctx->phy, &tuple,
	    RTW88_POWER_REGD));
	sc->sc_channel = tuple.primary;
	sc->sc_phy.channel = tuple;
	sc->sc_phy.channel_valid = 1;
	STEP("HCI start", sc->sc_hci->start(sc));
	STEP("firmware information", rtw8821c_fw_init(sc));
	/* Antenna ownership is configured by the chip's Wi-Fi-only slice. */
	STEP("Wi-Fi antenna", rtw8821c_coex_init(sc, ctx->phy.efuse.bt_setting));
#undef STEP
	sc->sc_rf_enabled = 1;
	sc->sc_edca_error = sc->sc_ra.error = 0;
	sc->sc_stats.ra_report_valid = 0;
	sc->sc_started = 1;
	sc->sc_bootstrapping = 0;
	goto release;
fail:
	printf("rtwce: RTL8821C startup %s failed: %d\n", stage, error);
	(void)rtw88_stop(sc);
release:
	if (sc->sc_ops->firmware_release != NULL)
		sc->sc_ops->firmware_release(sc->sc_bus);
	return error;
}

static int
rtw8821c_channel(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *tuple)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	uint32_t gen;
	uint8_t pause;
	int error;

	if (ctx == NULL || !sc->sc_started || !sc->sc_running ||
	    sc->sc_stop_requested)
		return ENXIO;
	error = rtw8821c_tuple_check(sc, tuple);
	if (error != 0)
		return error;
	if (sc->sc_phy.channel_valid &&
	    sc->sc_phy.channel.primary == tuple->primary &&
	    sc->sc_phy.channel.center == tuple->center &&
	    sc->sc_phy.channel.width_mhz == tuple->width_mhz)
		return 0;
	gen = sc->sc_generation;
	sc->sc_phy.channel_valid = 0;
	ctx->needs_iqk = 1;
	rtw88_ra_invalidate(sc);
	error = sc->sc_ops->read_1(sc->sc_bus, 0x0522, &pause);
	if (error != 0)
		return error;
	error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, 0xff);
	if (error == 0)
		error = rtw8821c_phy_current(ctx);
	if (error == 0)
		error = rtw8821c_set_channel_tuple(&ctx->phy, tuple);
	if (error == 0)
		error = rtw8821c_phy_current(ctx);
	if (error == 0)
		error = rtw8821c_set_tx_power_tuple(&ctx->phy, tuple, RTW88_POWER_REGD);
	if (error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		error = EINTR;
	if (error == 0)
		error = sc->sc_ops->write_1(sc->sc_bus, 0x0522, pause);
	if (error == 0 && (sc->sc_stop_requested || sc->sc_generation != gen))
		error = EINTR;
	if (error != 0) {
		sc->sc_stop_requested = 1;
		return error;
	}
	sc->sc_channel = tuple->primary;
	sc->sc_phy.channel = *tuple;
	sc->sc_phy.channel_valid = 1;
	return 0;
}

static int
rtw8821c_stop(struct rtw88_softc *sc)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	if (ctx != NULL) {
		ctx->needs_iqk = 1;
		ctx->phy.post_valid = 0;
	}
	return rtw8821c_power(sc, 0);
}

static int
rtw8821c_tx_desc(struct rtw88_softc *sc, uint8_t *out, size_t size,
    const struct rtw88_tx_info *info)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	/* Firmware's beacon DMA runs with the radio still disabled. All
	 * operational frames require the current channel's completed IQK. */
	if (ctx == NULL || (ctx->needs_iqk &&
	    (!sc->sc_bootstrapping || sc->sc_started)))
		return EACCES;
	return rtw8821c_tx_encode(out, size, info);
}

static int
rtw8821c_rx_desc(struct rtw88_softc *sc, const uint8_t *data,
    size_t size, struct rtw88_rx_info *info)
{
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	int error;

	error = rtw8821c_rx_decode(data, size, info);
	if (error != 0)
		return error;
	if (ctx != NULL)
		(void)rtw8821c_signal_decode(data + info->driver_info_offset,
		    info->driver_info_size, info, ctx->phy.efuse.rfe_option,
		    ctx->generation, sc->sc_generation, &sc->sc_phy.channel,
		    sc->sc_phy.channel_valid && !sc->sc_stop_requested, &info->signal);
	return 0;
}

static int
rtw8821c_diag(struct rtw88_softc *sc, uint32_t *out)
{
	static const uint16_t regs[] = {0x00b4, 0x00b0, 0x10bc,
	    0x10b8, 0x03b4, 0x0300};
	struct rtw8821c_runtime *ctx = sc->sc_chip_private;
	uint32_t values[RTW88_DIAG_REGS] = {0};
	int error;

	if (ctx == NULL || out == NULL || !sc->sc_running)
		return ENXIO;
	if (ctx->current != NULL)
		return EBUSY;
	for (unsigned i = 0; i < __arraycount(regs); i++) {
		error = sc->sc_ops->read_4(sc->sc_bus, regs[i], &values[i]);
		if (error != 0)
			return error;
	}
	error = rtw8821c_read_rf(&ctx->phy, 0x18, &values[6]);
	if (error != 0)
		return error;
	memcpy(out, values, sizeof(values));
	return 0;
}

static int
rtw8821c_loopback(struct rtw88_softc *sc, int mode)
{
	(void)sc;
	(void)mode;
	/* No validated RTL8821C diagnostic loopback protocol. */
	return EOPNOTSUPP;
}

const struct rtw88_chip_ops rtw8821c_chip_ops = {
	.caps = {
		.bands = RTW88_BAND_2G,
		.widths = RTW88_WIDTH_20,
		.nss = 1,
		.tx_desc_size = RTW8821C_TX_PACKET_DESC_SIZE,
		.rx_desc_size = RTW8821C_RX_PACKET_DESC_SIZE,
		.features = RTW88_CAP_WPA2 | RTW88_CAP_WME,
	},
	.attach_check = rtw8821c_attach,
	.detach = rtw8821c_detach,
	.identify = rtw8821c_discover,
	.start = rtw8821c_start,
	.stop = rtw8821c_stop,
	.fw_init = rtw8821c_fw_init,
	.channel = rtw8821c_channel,
	.prepare_station = rtw8821c_prepare_station,
	.station = rtw88_wcpu_station,
	.edca = rtw88_wcpu_edca,
	.ra = rtw88_wcpu_ra,
	.mailbox_begin = rtw88_wcpu_mailbox_begin,
	.mailbox_end = rtw88_wcpu_mailbox_end,
	.command = rtw88_wcpu_command,
	.diag = rtw8821c_diag,
	.loopback = rtw8821c_loopback,
	.tuple_check = rtw8821c_tuple_check,
	.tx_desc = rtw8821c_tx_desc,
	.rx_desc = rtw8821c_rx_desc,
};
