/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native core ownership binding for the existing RTL8723D boot protocol.
 * AI-assisted. No PCI bus-master/DMA state is fabricated for SDIO. */
#include <dev/ic/rtw8723d_core_boot.h>
#include <dev/ic/rtw88_io.h>

static bool
rtw8723d_core_cancelled(void *cookie)
{
	struct rtw8723d_core_boot *ctx = cookie;

	return ctx->core->sc_stop_requested ||
	    ctx->core->sc_generation != ctx->generation ||
	    ctx->hooks.revoked(ctx->hooks.cookie);
}

static void
rtw8723d_core_wait(void *cookie, unsigned ms)
{
	struct rtw8723d_core_boot *ctx = cookie;

	ctx->hooks.wait_ms(ctx->hooks.cookie, ms);
}

static int
rtw8723d_core_window(void *cookie, uint32_t address,
    const uint8_t *data, size_t size)
{
	struct rtw8723d_core_boot *ctx = cookie;

	if (rtw8723d_core_cancelled(ctx))
		return ECANCELED;
	return ctx->core->sc_hci->write_window(ctx->core, address, data, size);
}

int
rtw8723d_core_boot_init(struct rtw8723d_core_boot *ctx, struct rtw88_softc *sc,
    const struct rtw8723d_core_hooks *hooks)
{
	struct rtw88_io io;

	if (ctx == NULL || sc == NULL || sc->sc_hci == NULL ||
	    sc->sc_hci->write_window == NULL || sc->sc_hci->set_data_mode == NULL ||
	    hooks == NULL || hooks->wait_ms == NULL || hooks->revoked == NULL)
		return EINVAL;
	if (ctx->owned || ctx->firmware_booted || ctx->failed || sc->sc_powered ||
	    sc->sc_power_dirty || sc->sc_started || sc->sc_running)
		return EBUSY;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	if (io.error != 0)
		return io.error;
	ctx->core = sc;
	ctx->hooks = *hooks;
	ctx->generation = sc->sc_generation;
	ctx->ops = (struct rtw8723d_boot_ops) {
		.write_window = rtw8723d_core_window,
		.wait_ms = rtw8723d_core_wait,
		.cancelled = rtw8723d_core_cancelled, .cookie = ctx
	};
	return 0;
}

int
rtw8723d_core_power_off(struct rtw8723d_core_boot *ctx)
{
	struct rtw88_io io;
	struct rtw88_softc *sc;
	int error;

	if (ctx == NULL || (sc = ctx->core) == NULL)
		return EINVAL;
	if (!ctx->owned)
		return (sc->sc_powered || sc->sc_power_dirty) ? EBUSY : 0;
	/* No generation/cancellation gate: STOP must retire the old owner. */
	error = sc->sc_hci->set_data_mode(sc, false);
	if (error == 0) {
		rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
		error = rtw8723d_power_off(&io, &ctx->ops);
	}
	sc->sc_power_error = error;
	if (error != 0) {
		ctx->failed = true;
		return error;
	}
	sc->sc_powered = sc->sc_power_dirty = 0;
	ctx->owned = ctx->firmware_booted = ctx->failed = false;
	return 0;
}

int
rtw8723d_core_power_on(struct rtw8723d_core_boot *ctx)
{
	struct rtw88_io io;
	struct rtw88_softc *sc;
	int error;

	if (ctx == NULL || (sc = ctx->core) == NULL)
		return EINVAL;
	if (ctx->owned || ctx->failed || sc->sc_power_dirty || sc->sc_powered)
		return EBUSY;
	if (!sc->sc_resources_ready)
		return ENXIO;
	if (rtw8723d_core_cancelled(ctx))
		return ECANCELED;
	error = sc->sc_hci->set_data_mode(sc, false);
	if (error != 0)
		return error;
	ctx->owned = true;
	sc->sc_power_dirty = 1;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	/* Claim power cleanup before this uncertain mask write. The legacy
	 * power helper preserves its caller's interrupt mask across transitions. */
	rtw88_io_wr(&io, RTW8723D_SDIO_LOCAL_BASE + 0x14, 4, 0);
	error = io.error;
	if (error == 0)
		error = rtw8723d_power_on(&io, &ctx->ops);
	if (error == 0 && rtw8723d_core_cancelled(ctx))
		error = ECANCELED;
	if (error == 0) {
		sc->sc_powered = 1;
		error = sc->sc_hci->set_data_mode(sc, true);
	}
	if (error == 0 && rtw8723d_core_cancelled(ctx))
		error = ECANCELED;
	if (error != 0) {
		ctx->failed = true;
		(void)rtw8723d_core_power_off(ctx);
		return error;
	}
	sc->sc_power_error = 0;
	return 0;
}

int
rtw8723d_core_firmware(struct rtw8723d_core_boot *ctx,
    const uint8_t *bytes, size_t size)
{
	struct rtw8723d_fw fw;
	struct rtw88_io io;
	int error;

	if (ctx == NULL || ctx->core == NULL)
		return EINVAL;
	if (!ctx->owned || !ctx->core->sc_powered)
		return ENXIO;
	if (ctx->failed)
		return EBUSY;
	if (ctx->firmware_booted)
		return EALREADY;
	if (rtw8723d_core_cancelled(ctx))
		return ECANCELED;
	error = rtw8723d_fw_parse(bytes, size, &fw);
	if (error != 0)
		return error;
	rtw88_io_init(&io, ctx->core->sc_ops, ctx->core->sc_bus);
	error = rtw8723d_fw_boot(&io, &fw, &ctx->ops);
	if (error == 0 && rtw8723d_core_cancelled(ctx))
		error = ECANCELED;
	if (error != 0) {
		ctx->failed = true;
		(void)rtw8723d_core_power_off(ctx);
		return error;
	}
	ctx->firmware_booted = true;
	return 0;
}
