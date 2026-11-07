/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Original selected-chip lifecycle binding, AI-assisted. The optional native
 * chip class includes firmware/coexistence code. The frontend
 * must still bind its parent/HCI/netif lifetime before any operational use. */
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw8723d_chip.h>
#include <dev/ic/rtw8723d_ra.h>
#include <dev/ic/rtw8723d_fw_runtime.h>
#include <dev/ic/rtw8723d_coex.h>
#include <dev/ic/rtw8723d_station.h>
#include <dev/ic/rtw8723d_tx.h>
#include <dev/ic/rtw8723d_rx.h>
#include <dev/ic/rtw88_executor.h>
#include <dev/ic/rtw88_io.h>

static int
rtw8723d_chip_identify(struct rtw88_softc *sc)
{
	struct rtw8723d_runtime *ctx = sc->sc_chip_private;
	int error;

	if (ctx == NULL || ctx->boot.core != sc || !rtw88_executor_owned(sc))
		return EINVAL;
	if (ctx->identified)
		return 0;
	error = rtw8723d_core_read_identity(&ctx->boot, &ctx->identity);
	if (error != 0)
		return error;
	ctx->config.efuse = ctx->identity.efuse;
	ctx->config.condition.cut = ctx->identity.chip.cut;
	ctx->config.condition.rfe = ctx->identity.efuse.rfe;
	ctx->config.condition.interface = RTW8723D_PHY_SDIO;
	ctx->identified = true;
	return 0;
}

static int
rtw8723d_chip_stop(struct rtw88_softc *sc)
{
	struct rtw8723d_runtime *ctx = sc->sc_chip_private;
	int error;

	if (ctx == NULL || ctx->boot.core != sc || !rtw88_executor_owned(sc))
		return EINVAL;
	/* Generic STOP has already revoked the old generation and quiesced HCI.
	 * The existing power owner intentionally accepts that old cleanup lease. */
	error = rtw8723d_core_power_off(&ctx->boot);
	if (error == 0)
		memset(&ctx->phy, 0, sizeof(ctx->phy));
	return error;
}

static int
rtw8723d_chip_start(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *target)
{
	struct rtw8723d_runtime *ctx = sc->sc_chip_private;
	struct rtw8723d_core_hooks hooks;
	struct rtw8723d_core_phy_config config;
	struct rtw88_channel_tuple tuple;
	struct rtw8723d_fw parsed;
	struct rtw88_io io;
	const uint8_t *firmware = NULL;
	size_t size = 0;
	uint32_t generation;
	uint8_t power[RTW8723D_POWER_RATES];
	int error;

	if (ctx == NULL || ctx->boot.core != sc || target == NULL ||
	    !rtw88_executor_owned(sc))
		return EINVAL;
	tuple = *target;
	if (tuple.primary < 1 || tuple.primary > 13 ||
	    tuple.center != tuple.primary || tuple.width_mhz != 20)
		return EOPNOTSUPP;
	if (!ctx->identified)
		return ENXIO;
	config = ctx->config;
	if (memcmp(&config.efuse, &ctx->identity.efuse,
	    sizeof(config.efuse)) != 0 ||
	    config.condition.cut != ctx->identity.chip.cut ||
	    config.condition.rfe != ctx->identity.efuse.rfe ||
	    config.condition.interface != RTW8723D_PHY_SDIO ||
	    config.condition.package > 15)
		return EINVAL;
	if (ctx->boot.owned || ctx->boot.failed || sc->sc_powered ||
	    sc->sc_power_dirty || sc->sc_started || sc->sc_running ||
	    sc->sc_dma_master || sc->sc_dma_error || sc->sc_hci_error ||
	    sc->sc_power_error)
		return EBUSY;
	if (sc->sc_generation >= UINT32_MAX - 1)
		return EOVERFLOW;
	if (sc->sc_ops->firmware_load == NULL ||
	    sc->sc_ops->firmware_release == NULL)
		return ENODEV;
	/* Pure admission precedes a loader, transport acquisition or chip write. */
	error = rtw8723d_power_compute(&config.efuse, &config.profile,
	    &config.power, tuple.primary, power);
	if (error != 0)
		return error;
	generation = sc->sc_generation;
	error = sc->sc_ops->firmware_load(sc->sc_bus, "rtw88",
	    "rtw8723d_fw.bin", &firmware, &size);
	if (error != 0)
		return error;
	if (sc->sc_generation != generation ||
	    ctx->boot.hooks.revoked(ctx->boot.hooks.cookie)) {
		error = ECANCELED;
		goto release;
	}
	error = rtw8723d_fw_parse(firmware, size, &parsed);
	if (error != 0)
		goto release;
	if (sc->sc_generation != generation ||
	    ctx->boot.hooks.revoked(ctx->boot.hooks.cookie)) {
		error = ECANCELED;
		goto release;
	}
	sc->sc_generation++;
	sc->sc_stop_requested = 0;
	sc->sc_bootstrapping = 1;
	sc->sc_rf_enabled = 0;
	sc->sc_phy.channel_valid = 0;
	rtw88_ra_invalidate(sc);
	hooks = ctx->boot.hooks;
	error = rtw8723d_core_boot_init(&ctx->boot, sc, &hooks);
	if (error == 0 && (sc->sc_generation != ctx->boot.generation ||
	    sc->sc_stop_requested || hooks.revoked(hooks.cookie)))
		error = ECANCELED;
	if (error == 0)
		error = sc->sc_hci->prepare(sc);
	if (error == 0 && (sc->sc_generation != ctx->boot.generation ||
	    sc->sc_stop_requested || hooks.revoked(hooks.cookie)))
		error = ECANCELED;
	if (error == 0)
		error = rtw8723d_core_power_on(&ctx->boot);
	if (error == 0)
		error = rtw8723d_core_firmware(&ctx->boot, firmware, size);
	if (error == 0)
		error = rtw8723d_mailbox_reset(&ctx->mailbox, sc->sc_generation);
	if (error == 0)
		error = sc->sc_hci->setup(sc);
	config.channel = tuple.primary;
	if (error == 0)
		error = rtw8723d_core_phy_configure(&ctx->boot,
		    &config, &ctx->phy);
	if (error == 0) {
		ctx->config = config;
		sc->sc_channel = tuple.primary;
		sc->sc_phy.generation = sc->sc_generation;
		sc->sc_phy.paths = 1;
		sc->sc_phy.channel = tuple;
		error = sc->sc_hci->start(sc);
	}
	if (error == 0)
		error = rtw8723d_fw_runtime_init(sc);
	if (error == 0)
		error = rtw8723d_coex_start(ctx);
	if (error == 0)
		error = rtw8723d_runtime_check(ctx);
	if (error == 0) {
		/* Open management queues only. The managed station transaction
		 * controls data admission after association. */
		rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
		rtw88_io_wr(&io, 0x522, 1, 0x0f);
		error = io.error;
	}
	if (error == 0)
		error = rtw8723d_runtime_check(ctx);
	if (error != 0) {
		/* Retain first startup error. Generic stop quiesces the transport
		 * before releasing the single chip-power owner. */
		(void)rtw88_stop(sc);
	} else {
		ctx->config = config;
		sc->sc_phy.channel_valid = 1;
		sc->sc_edca_error = 0;
		sc->sc_ra.error = 0;
		sc->sc_stats.ra_report_valid = 0;
		sc->sc_rf_enabled = 1;
		sc->sc_started = 1;
		sc->sc_bootstrapping = 0;
	}
release:
	sc->sc_ops->firmware_release(sc->sc_bus);
	return error;
}

static int
rtw8723d_chip_tx_desc(struct rtw88_softc *sc __unused, uint8_t *out,
    size_t size, const struct rtw88_tx_info *info)
{

	return rtw8723d_tx_encode(out, size, info);
}

static int
rtw8723d_chip_diag(struct rtw88_softc *sc __unused, uint32_t *out __unused)
{

	return EOPNOTSUPP;
}

static int
rtw8723d_chip_loopback(struct rtw88_softc *sc __unused, int mode __unused)
{

	return EOPNOTSUPP;
}

static unsigned
rtw8723d_station_commands(const struct rtw88_softc *sc __unused,
    int linked __unused)
{

	return 1;
}

const struct rtw88_chip_ops rtw8723d_chip_ops = {
	/* Initial implemented subset, not the complete chipset specification. */
	.caps = { .bands = RTW88_BAND_2G, .widths = RTW88_WIDTH_20, .nss = 1,
	    .tx_desc_size = 40, .rx_desc_size = 24,
	    .ht_mcs = 0,
	    .features = RTW88_CAP_WPA2 },
	.identify = rtw8723d_chip_identify,
	.start = rtw8723d_chip_start,
	.stop = rtw8723d_chip_stop,
	.fw_init = rtw8723d_fw_runtime_init,
	.channel = rtw8723d_runtime_channel,
	.station_commands = rtw8723d_station_commands,
	.station = rtw8723d_runtime_station,
	.edca = rtw8723d_runtime_edca,
	.ra = rtw8723d_runtime_ra,
	.mailbox_begin = rtw8723d_runtime_mailbox_begin,
	.mailbox_end = rtw8723d_runtime_mailbox_end,
	.command = rtw8723d_runtime_command,
	.diag = rtw8723d_chip_diag,
	.loopback = rtw8723d_chip_loopback,
	.tx_desc = rtw8723d_chip_tx_desc,
	.rx_desc = rtw8723d_rx_desc,
	.station_macid = RTW8723D_STATION_MACID
};

int
rtw8723d_chip_bind(struct rtw88_softc *sc, struct rtw8723d_runtime *ctx,
    const struct rtw8723d_core_hooks *hooks)
{
	int error;

	if (sc == NULL || ctx == NULL || hooks == NULL || sc->sc_chip != NULL ||
	    sc->sc_chip_private != NULL || ctx->boot.core != NULL ||
	    ctx->identified || ctx->boot.owned || ctx->boot.failed ||
	    ctx->mailbox.generation != 0 || sc->sc_hci == NULL ||
	    !sc->sc_hci->no_host_dma || !rtw88_hci_valid(sc))
		return EINVAL;
	error = rtw8723d_core_boot_init(&ctx->boot, sc, hooks);
	if (error != 0)
		return error;
	sc->sc_chip_private = ctx;
	sc->sc_chip = &rtw8723d_chip_ops;
	return 0;
}
