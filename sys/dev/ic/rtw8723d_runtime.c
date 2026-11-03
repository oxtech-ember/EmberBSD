/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native RTL8723D chip_ops control binding, AI-assisted. */
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw8723d_runtime.h>
#include <dev/ic/rtw8723d_channel.h>
#include <dev/ic/rtw8723d_station.h>
#include <dev/ic/rtw8723d_edca.h>
#include <dev/ic/rtw88_executor.h>
#include <dev/ic/rtw88_io.h>

int
rtw8723d_runtime_check(void *cookie)
{
	struct rtw8723d_runtime *ctx = cookie;
	struct rtw88_softc *sc;

	if (ctx == NULL || (sc = ctx->boot.core) == NULL ||
	    sc->sc_chip_private != ctx || ctx->boot.hooks.revoked == NULL)
		return EINVAL;
	if (!rtw88_executor_owned(sc))
		return EPERM;
	if (sc->sc_generation == 0 ||
	    sc->sc_generation != ctx->boot.generation || sc->sc_stop_requested ||
	    ctx->boot.hooks.revoked(ctx->boot.hooks.cookie))
		return ECANCELED;
	if (ctx->boot.failed)
		return EBUSY;
	if (!ctx->identified || !ctx->boot.owned || !ctx->boot.firmware_booted ||
	    !sc->sc_resources_ready || !sc->sc_powered)
		return ENXIO;
	return 0;
}

static int
rtw8723d_runtime_io(struct rtw88_softc *sc, struct rtw8723d_runtime **out,
    struct rtw88_io *io, bool operational)
{
	struct rtw8723d_runtime *ctx;
	int error;

	if (sc == NULL || (ctx = sc->sc_chip_private) == NULL ||
	    ctx->boot.core != sc)
		return EINVAL;
	if ((error = rtw8723d_runtime_check(ctx)) != 0)
		return error;
	if (operational && (!sc->sc_running || !sc->sc_started ||
	    !sc->sc_rf_enabled || !sc->sc_phy.channel_valid ||
	    sc->sc_phy.generation != sc->sc_generation))
		return ENXIO;
	rtw88_io_init(io, sc->sc_ops, sc->sc_bus);
	if (io->error != 0)
		return io->error;
	*out = ctx;
	return 0;
}

static int
rtw8723d_runtime_fault(struct rtw8723d_runtime *ctx, int error)
{
	struct rtw88_io cleanup;
	struct rtw88_softc *sc = ctx->boot.core;

	/* Keep ownership until HCI quiesce followed by the power owner's off.
	 * Never let a sticky transaction error suppress an independent TX close. */
	sc->sc_phy.channel_valid = 0;
	sc->sc_rf_enabled = 0;
	ctx->boot.failed = true;
	rtw88_ra_invalidate(sc);
	if (sc->sc_resources_ready && sc->sc_powered && ctx->boot.owned) {
		rtw88_io_init(&cleanup, sc->sc_ops, sc->sc_bus);
		rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
	}
	return error;
}

int
rtw8723d_runtime_channel(struct rtw88_softc *sc,
    const struct rtw88_channel_tuple *target)
{
	struct rtw8723d_runtime *ctx;
	struct rtw88_channel_tuple tuple;
	struct rtw88_io io;
	uint8_t power[RTW8723D_POWER_RATES], pause;
	int error;

	if (target == NULL)
		return EINVAL;
	tuple = *target;
	if (tuple.primary < 1 || tuple.primary > 13 ||
	    tuple.center != tuple.primary || tuple.width_mhz != 20)
		return EOPNOTSUPP;
	error = rtw8723d_runtime_io(sc, &ctx, &io, true);
	if (error != 0)
		return error;
	error = rtw8723d_power_compute(&ctx->config.efuse, &ctx->config.profile,
	    &ctx->config.power, tuple.primary, power);
	if (error != 0)
		return error;
	/* Avoid repeating channel13 PSD and power writes for unchanged input. */
	if (sc->sc_phy.channel.primary == tuple.primary &&
	    sc->sc_phy.channel.center == tuple.center &&
	    sc->sc_phy.channel.width_mhz == tuple.width_mhz)
		return 0;
	pause = rtw88_io_rd(&io, 0x522, 1);
	if (io.error != 0)
		return io.error; /* No write attempted; old channel remains valid. */
	if ((error = rtw8723d_runtime_check(ctx)) != 0)
		return error;
	sc->sc_phy.channel_valid = 0;
	rtw88_ra_invalidate(sc);
	rtw88_io_wr(&io, 0x522, 1, 0xff);
	if ((error = io.error) == 0)
		error = rtw8723d_channel20(&io, &ctx->boot.ops, tuple.primary);
	if (error == 0)
		error = rtw8723d_power_program(&io, &ctx->config.efuse,
		    &ctx->config.profile, &ctx->config.power, tuple.primary,
		    rtw8723d_runtime_check, ctx);
	if (error == 0)
		error = rtw8723d_runtime_check(ctx);
	if (error == 0) {
		rtw88_io_wr(&io, 0x522, 1, pause);
		error = io.error;
	}
	if (error == 0)
		error = rtw8723d_runtime_check(ctx);
	if (error != 0)
		return rtw8723d_runtime_fault(ctx, error);
	sc->sc_channel = tuple.primary;
	sc->sc_phy.channel = tuple;
	sc->sc_phy.channel_valid = 1;
	ctx->config.channel = ctx->phy.channel = tuple.primary;
	return 0;
}

int
rtw8723d_runtime_station(struct rtw88_softc *sc, const uint8_t *bssid,
    unsigned aid, unsigned rates, unsigned slot, int linked)
{
	struct rtw8723d_runtime *ctx;
	struct rtw8723d_station_config cfg = {0};
	struct rtw88_io io;
	uint8_t any = 0;
	unsigned i;
	int error;

	if (linked != 0 && linked != 1)
		return EINVAL;
	if (linked && bssid == NULL)
		return EINVAL;
	if (rates == 0 || (rates & ~0xfffU) != 0 ||
	    (slot != 9 && slot != 20) || (linked && (aid == 0 || aid > 2007)))
		return EINVAL;
	if (linked) {
		for (i = 0; i < 6; i++)
			any |= bssid[i];
		if (any == 0 || (bssid[0] & 1) != 0)
			return EINVAL;
		memcpy(cfg.bssid, bssid, sizeof(cfg.bssid));
	}
	error = rtw8723d_runtime_io(sc, &ctx, &io, true);
	if (error != 0)
		return error;
	if (!ctx->mailbox.active || ctx->mailbox.generation != sc->sc_generation ||
	    ctx->mailbox.uncertain || ctx->mailbox.error != 0 ||
	    ctx->mailbox.sent >= ctx->mailbox.total || ctx->mailbox.next > 3 ||
	    ((ctx->mailbox.first + ctx->mailbox.sent) & 3) != ctx->mailbox.next)
		return ctx->mailbox.error != 0 ? ctx->mailbox.error : ESTALE;
	memcpy(cfg.mac, sc->sc_mac, sizeof(cfg.mac));
	cfg.aid = linked ? aid : 0;
	cfg.basic_rates = rates;
	cfg.slot = slot;
	cfg.linked = linked != 0;
	error = rtw8723d_station_apply(&io, &ctx->mailbox, sc->sc_generation,
	    &cfg, rtw8723d_runtime_check, ctx);
	if (error != 0)
		return rtw8723d_runtime_fault(ctx, error);
	sc->sc_linked = linked;
	return 0;
}

int
rtw8723d_runtime_edca(struct rtw88_softc *sc,
    const struct rtw88_edca_params *params, unsigned slot)
{
	struct rtw8723d_runtime *ctx;
	struct rtw88_io io;
	struct rtw88_edca_params input[RTW88_WME_NAC];
	uint32_t words[RTW88_WME_NAC];
	int error;

	if (params == NULL)
		return EINVAL;
	memcpy(input, params, sizeof(input));
	error = rtw88_edca_encode(input, slot, 10, words);
	if (error != 0)
		return error;
	error = rtw8723d_runtime_io(sc, &ctx, &io, true);
	if (error != 0)
		return error;
	error = rtw8723d_edca_apply(&io, input, slot,
	    rtw8723d_runtime_check, ctx);
	sc->sc_edca_error = error;
	return error == 0 ? 0 : rtw8723d_runtime_fault(ctx, error);
}

int
rtw8723d_runtime_mailbox_begin(struct rtw88_softc *sc, unsigned count,
    int *ready)
{
	struct rtw8723d_runtime *ctx;
	struct rtw88_io io;
	int error;

	if (ready == NULL)
		return EINVAL;
	*ready = 0;
	if (count > 3)
		return EINVAL;
	error = rtw8723d_runtime_io(sc, &ctx, &io, false);
	if (error != 0)
		return error;
	return rtw8723d_mailbox_begin(&io, &ctx->mailbox, sc->sc_generation,
	    count, ready, rtw8723d_runtime_check, ctx);
}

int
rtw8723d_runtime_mailbox_end(struct rtw88_softc *sc, int error)
{
	struct rtw8723d_runtime *ctx;
	int result;

	if (sc == NULL || (ctx = sc->sc_chip_private) == NULL ||
	    ctx->boot.core != sc || !rtw88_executor_owned(sc))
		return error != 0 ? error : EINVAL;
	/* STOP/generation revocation must still retire an owned reservation. */
	result = rtw8723d_mailbox_end(&ctx->mailbox, sc->sc_generation, error);
	if (ctx->mailbox.uncertain)
		return rtw8723d_runtime_fault(ctx, result);
	return result;
}

int
rtw8723d_runtime_command(struct rtw88_softc *sc, uint8_t id,
    const void *payload, size_t length)
{
	struct rtw8723d_runtime *ctx;
	struct rtw88_io io;
	uint8_t bytes[7];
	int error;

	if (length > sizeof(bytes))
		return EMSGSIZE;
	if (length != 0 && payload == NULL)
		return EINVAL;
	if (length != 0)
		memcpy(bytes, payload, length);
	error = rtw8723d_runtime_io(sc, &ctx, &io, false);
	if (error != 0)
		return error;
	error = rtw8723d_mailbox_send(&io, &ctx->mailbox, sc->sc_generation,
	    id, bytes, length, rtw8723d_runtime_check, ctx);
	if (ctx->mailbox.uncertain)
		return rtw8723d_runtime_fault(ctx, error);
	return error;
}
