/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Original native policy, AI-assisted. Wire fields/ID: dual-BSD Linux
 * adc218676eef25575469234709c2d87185ca223a rtw88/fw.h:543-550,649-674.
 * Rate IDs and masks: main.h:221-238, main.c:1015-1115; initial level1:
 * main.c:325-350. This explicitly selects BW20, full update, no LDPC/SGI/
 * VHT and disabled firmware power training; it does not infer donor defaults
 * from the unavailable fw.c or serialize an 8822C-specific configuration. */
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw8723d_ra.h>
#include <dev/ic/rtw8723d_runtime.h>
#include <dev/ic/rtw8723d_station.h>
#include <dev/ic/rtw88_internal.h>
#include <dev/ic/rtw88_io.h>

int
rtw8723d_runtime_ra(struct rtw88_softc *sc, const struct rtw88_ra_config *cfg)
{
	struct rtw88_ra_config input;
	struct rtw88_ra_state ra = {0};
	struct rtw8723d_runtime *ctx;
	uint8_t payload[7] = { RTW8723D_STATION_MACID, 0, 0x40, 0, 0, 0, 0 };
	unsigned mask;
	int error;

	if (sc == NULL || cfg == NULL || (ctx = sc->sc_chip_private) == NULL ||
	    ctx->boot.core != sc)
		return EINVAL;
	input = *cfg;
	if (input.epoch == 0 || input.channel < 1 || input.channel > 13 ||
	    input.common_rates == 0 || (input.common_rates & ~0xfffU) != 0 ||
	    input.use_protection > 1)
		return EINVAL;
	if (input.phy != RTW88_RA_LEGACY || input.sgi != 0 ||
	    input.ldpc != 0 || input.ht_mcs != 0 || input.ht_operation != 0 ||
	    input.vht_tx_map != 0 || input.profile_generation != 0 ||
	    input.ht.peer_cap != 0 || input.ht.mtu != 0 ||
	    input.ht.max_mpdu != 0 || input.ht.rts_threshold != 0 ||
	    input.ht.secondary != 0 || input.ht.qualification != 0) {
		return EOPNOTSUPP;
	}
	mask = input.common_rates;
	if (input.mode == RTW88_FIXED_11B) {
		if ((mask & ~0xfU) != 0)
			return EINVAL;
	} else if (input.mode != RTW88_FIXED_11G) {
		return EOPNOTSUPP;
	}
	if (input.phy == RTW88_RA_LEGACY) {
		if ((mask & ~0xfU) == 0)
			ra.rate_id = 8; /* B. */
		else if ((mask & 0xf) == 0)
			ra.rate_id = 7; /* G. */
		else {
			ra.rate_id = 6; /* BG: bounded CCK fallback, all offered OFDM. */
			mask &= 0xff5;
		}
	}
	if (mask == 0)
		return EOPNOTSUPP;
	if ((error = rtw8723d_runtime_check(ctx)) != 0)
		return error;
	if (!sc->sc_running || !sc->sc_started || !sc->sc_rf_enabled ||
	    !sc->sc_linked || !sc->sc_phy.channel_valid ||
	    sc->sc_phy.generation != sc->sc_generation ||
	    sc->sc_channel != input.channel ||
	    sc->sc_phy.channel.primary != input.channel ||
	    sc->sc_phy.channel.center != input.channel ||
	    sc->sc_phy.channel.width_mhz != 20)
		return ENXIO;
	if (sc->sc_edca_error != 0 || sc->sc_ra.error != 0)
		return sc->sc_edca_error != 0 ? sc->sc_edca_error : sc->sc_ra.error;
	ra.epoch = input.epoch;
	ra.generation = sc->sc_generation;
	ra.channel = input.channel;
	ra.common_rates = input.common_rates;
	ra.mask = mask;
	ra.phy = input.phy;
	ra.ht_mcs = input.ht_mcs;
	ra.ht_operation = input.ht_operation;
	ra.profile_generation = input.profile_generation;
	ra.ht = input.ht;
	ra.sgi = input.sgi;
	ra.ldpc = input.ldpc;
	ra.tuple = sc->sc_phy.channel;
	ra.configured = 1;
	if (input.use_protection) {
		/* Keep the shared core's conservative CCK software fallback. */
		if ((sc->sc_basic_rates & input.common_rates & 0xf) == 0)
			return EOPNOTSUPP;
		ra.protection_limited = 1;
		sc->sc_ra = ra;
		return 0;
	}
	if (!ctx->mailbox.active || ctx->mailbox.generation != sc->sc_generation ||
	    ctx->mailbox.sent >= ctx->mailbox.total || ctx->mailbox.uncertain ||
	    ctx->mailbox.error != 0)
		return ESTALE;
	payload[1] = ra.rate_id | (1U << 5);
	payload[3] = mask;
	payload[4] = mask >> 8;
	payload[5] = mask >> 16;
	payload[6] = mask >> 24;
	error = rtw8723d_runtime_command(sc, 0x40, payload, sizeof(payload));
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
