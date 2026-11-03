/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native binding of chip stages, AI-assisted. */
#include <dev/ic/rtw8723d_core_phy.h>
#include <dev/ic/rtw8723d_mac.h>
#include <dev/ic/rtw8723d_startup.h>
#include <dev/ic/rtw8723d_lck.h>
#include <dev/ic/rtw8723d_iqk_cal.h>
#include <dev/ic/rtw8723d_channel.h>
#include <dev/ic/rtw88_io.h>

static int
rtw8723d_core_phy_check(void *cookie)
{
	struct rtw8723d_core_boot *ctx = cookie;

	return ctx->core->sc_stop_requested ||
	    ctx->core->sc_generation != ctx->generation ||
	    ctx->hooks.revoked(ctx->hooks.cookie) ? ECANCELED : 0;
}

int
rtw8723d_core_phy_configure(struct rtw8723d_core_boot *ctx,
    const struct rtw8723d_core_phy_config *config,
    struct rtw8723d_core_phy_result *out)
{
	struct rtw8723d_core_phy_config input;
	struct rtw8723d_core_phy_result result = {0};
	struct rtw88_softc *sc;
	struct rtw88_io io;
	uint8_t power[RTW8723D_POWER_RATES];
	int error;

	if (ctx == NULL || config == NULL || out == NULL ||
	    (sc = ctx->core) == NULL || ctx->hooks.revoked == NULL ||
	    ctx->ops.cancelled == NULL || ctx->ops.wait_ms == NULL)
		return EINVAL;
	if (ctx->failed)
		return EBUSY;
	if (!ctx->owned || !ctx->firmware_booted || !sc->sc_powered ||
	    !sc->sc_resources_ready)
		return ENXIO;
	if (sc->sc_started || sc->sc_running || sc->sc_rf_enabled)
		return EBUSY;
	input = *config;
	if ((error = rtw8723d_core_phy_check(ctx)) != 0)
		return error;
	if (out->configured && out->generation == ctx->generation)
		return EALREADY;
	if (input.condition.interface != RTW8723D_PHY_SDIO ||
	    input.condition.cut != sc->sc_cut ||
	    input.condition.rfe != sc->sc_rfe ||
	    input.efuse.rfe != sc->sc_rfe || input.efuse.xtal != sc->sc_xtal)
		return EINVAL;
	/* Never touch MAC/PHY for a channel or power policy we cannot admit. */
	error = rtw8723d_power_compute(&input.efuse, &input.profile,
	    &input.power, input.channel, power);
	if (error != 0)
		return error;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	error = io.error;
	if (error == 0)
		error = rtw8723d_mac_configure(&io, rtw8723d_core_phy_check, ctx);
	if (error == 0)
		error = rtw8723d_swcrypto_configure(&io,
		    rtw8723d_core_phy_check, ctx);
	if (error == 0)
		error = rtw8723d_phy_configure(&io, &input.condition,
		    input.efuse.xtal, input.efuse.afe, rtw8723d_core_phy_check, ctx);
	if (error == 0)
		error = rtw8723d_dm_before_lck(&io, rtw8723d_core_phy_check, ctx);
	if (error == 0)
		error = rtw8723d_lck(&io, &ctx->ops);
	if (error == 0)
		error = rtw8723d_dm_after_lck(&io, rtw8723d_core_phy_check, ctx);
	if (error == 0)
		error = rtw8723d_iqk_calibrate(&io, &ctx->ops, &result.iqk);
	if (error == 0)
		error = rtw8723d_channel20(&io, &ctx->ops, input.channel);
	if (error == 0)
		error = rtw8723d_power_program(&io, &input.efuse, &input.profile,
		    &input.power, input.channel, rtw8723d_core_phy_check, ctx);
	if (error == 0)
		error = io.error;
	if (error == 0)
		error = rtw8723d_core_phy_check(ctx);
	if (error != 0) {
		ctx->failed = true;
		/* The owner must quiesce HCI before releasing chip power. */
		return error;
	}
	result.generation = ctx->generation;
	result.channel = input.channel;
	result.configured = true;
	*out = result;
	return 0;
}
