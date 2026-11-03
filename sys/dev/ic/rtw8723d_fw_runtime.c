/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Original native ownership check, AI-assisted. Protocol evidence: Linux
 * adc218676eef25575469234709c2d87185ca223a rtw88/fw.c:510-557 explicitly
 * returns without a command in both general-info and PHYDM-info senders
 * for WCPU_11N; rtw8723d.c:2156 identifies this chip as WCPU_11N. */
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw8723d_chip.h>
#include <dev/ic/rtw8723d_fw_runtime.h>

int
rtw8723d_fw_runtime_init(struct rtw88_softc *sc)
{
	struct rtw8723d_runtime *ctx;
	int error;

	if (sc == NULL || sc->sc_chip != &rtw8723d_chip_ops ||
	    (ctx = sc->sc_chip_private) == NULL || ctx->boot.core != sc)
		return EINVAL;
	if ((error = rtw8723d_runtime_check(ctx)) != 0)
		return error;
	if (sc->sc_packet_h2c.active)
		return EOPNOTSUPP;
	/* This documented 11N no-command path is intentional; firmware boot,
	 * HCI start and coexistence remain independent required startup gates. */
	return 0;
}
