/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Native RTL8723D identity/calibration-to-core binding, AI-assisted. */
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw8723d_core_identity.h>
#include <dev/ic/rtw88_io.h>

#ifdef _KERNEL
#define IDENTITY_ERROR(stage, error) \
	aprint_error("rtw8723d: identity %s failed: %d\n", stage, error)
#else
#define IDENTITY_ERROR(stage, error) ((void)0)
#endif

static bool
rtw8723d_identity_revoked(struct rtw8723d_core_boot *ctx)
{
	return ctx->core->sc_stop_requested ||
	    ctx->core->sc_generation != ctx->generation ||
	    ctx->hooks.revoked(ctx->hooks.cookie);
}

int
rtw8723d_core_read_identity(struct rtw8723d_core_boot *ctx,
    struct rtw8723d_core_identity *out)
{
	struct rtw8723d_core_identity value;
	struct rtw88_softc *sc;
	struct rtw88_io io;
	uint8_t physical[RTW8723D_EFUSE_PHYSICAL_SIZE];
	int error, off_error;

	if (ctx == NULL || out == NULL || (sc = ctx->core) == NULL)
		return EINVAL;
	if (ctx->owned || ctx->failed || ctx->firmware_booted ||
	    sc->sc_power_dirty || sc->sc_powered || sc->sc_started || sc->sc_running)
		return EBUSY;
	if (!sc->sc_resources_ready)
		return ENXIO;
	if (rtw8723d_identity_revoked(ctx))
		return ECANCELED;
	error = sc->sc_hci->set_data_mode(sc, false);
	if (error != 0) {
		IDENTITY_ERROR("data mode", error);
		return error;
	}
	if (rtw8723d_identity_revoked(ctx))
		return ECANCELED;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	error = rtw8723d_identify(&io, &value.chip);
	if (error != 0) {
		IDENTITY_ERROR("register identify", error);
		return error;
	}
	/* power_on already makes its own bounded error-unwind attempt. Do not
	 * duplicate that attempt here if acquisition failed with retained state. */
	error = rtw8723d_core_power_on(ctx);
	if (error != 0) {
		IDENTITY_ERROR("power on", error);
		return error;
	}
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	if (rtw8723d_identity_revoked(ctx))
		error = ECANCELED;
	else {
		error = rtw8723d_efuse_read(&io, physical);
		if (error != 0)
			IDENTITY_ERROR("efuse read", error);
	}
	if (error == 0) {
		error = rtw8723d_efuse_decode(physical, sizeof(physical), &value.efuse);
		if (error != 0)
			IDENTITY_ERROR("efuse decode", error);
	}
	/* Fresh independent IO inside the core owner: never let a sticky efuse
	 * error/cancel suppress the single bounded release attempt. */
	off_error = rtw8723d_core_power_off(ctx);
	if (off_error != 0)
		IDENTITY_ERROR("power off", off_error);
	if (error == 0)
		error = off_error;
	if (error == 0 && rtw8723d_identity_revoked(ctx))
		error = ECANCELED;
	if (error != 0)
		return error;
	memcpy(sc->sc_efuse_phys, physical, sizeof(physical));
	memcpy(sc->sc_mac, value.efuse.mac, sizeof(sc->sc_mac));
	sc->sc_sys_cfg1 = value.chip.sys_cfg1;
	sc->sc_cut = value.chip.cut;
	sc->sc_rfe = value.efuse.rfe;
	sc->sc_xtal = value.efuse.xtal;
	*out = value;
	return 0;
}
