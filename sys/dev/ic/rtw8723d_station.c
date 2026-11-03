/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Port0 register map: pinned dual-BSD Linux rtw88/mac80211.c rtw_vif_port,
 * main.c rtw_vif_port_config; reg.h RCR/BCN/RRSR/RXFLTMAP2; fw.c/fw.h
 * media-status H2C, adc218676eef25575469234709c2d87185ca223a.
 * Native fallible transaction/cleanup, AI-assisted. */
#include <dev/ic/rtw8723d_station.h>
#include <dev/ic/rtw8723d_mailbox.h>
#include <dev/ic/rtw88_io.h>

static bool
rtw8723d_station_live(struct rtw88_io *io, int (*check)(void *), void *cookie)
{
	if (io->error == 0 && check != NULL)
		io->error = check(cookie);
	return io->error == 0;
}

int
rtw8723d_station_apply(struct rtw88_io *io, struct rtw8723d_mailbox *box,
    uint32_t generation, const struct rtw8723d_station_config *input,
    int (*check)(void *), void *cookie)
{
	struct rtw8723d_station_config cfg;
	struct rtw88_io validation, cleanup;
	uint8_t any_mac = 0, any_bssid = 0, media[3] = { 0, RTW8723D_STATION_MACID, 0 };
	unsigned i;
	int error;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (input == NULL || box == NULL || generation == 0)
		return io->error = EINVAL;
	cfg = *input;
	for (i = 0; i < 6; i++) {
		any_mac |= cfg.mac[i];
		any_bssid |= cfg.bssid[i];
	}
	if (any_mac == 0 || (cfg.mac[0] & 1) != 0 || cfg.basic_rates == 0 ||
	    (cfg.basic_rates & ~0xfffU) != 0 || (cfg.slot != 9 && cfg.slot != 20))
		return io->error = EINVAL;
	if (cfg.linked ? (any_bssid == 0 || (cfg.bssid[0] & 1) != 0 ||
	    cfg.aid == 0 || cfg.aid > 2007) : (any_bssid != 0 || cfg.aid != 0))
		return io->error = EINVAL;
	/* Managed caller must reserve before any station mutation. */
	if (!box->active || box->generation != generation || box->uncertain ||
	    box->error != 0 || box->sent >= box->total || box->next > 3 ||
	    ((box->first + box->sent) & 3) != box->next)
		return io->error = box->error != 0 ? box->error : ESTALE;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	if (!rtw8723d_station_live(io, check, cookie))
		return io->error;

#define WRITE(addr, width, value) do { \
	if (rtw8723d_station_live(io, check, cookie)) \
		rtw88_io_wr(io, (addr), (width), (value)); \
} while (0)
#define MASK(addr, mask, value) do { \
	if (rtw8723d_station_live(io, check, cookie)) \
		rtw88_io_wr_mask(io, (addr), (mask), (value)); \
} while (0)
	/* Close data before making address and rate fields visible. */
	rtw88_io_set(io, 0x522, 1, 0x0f);
	MASK(0x100, 0x30000, 0);
	WRITE(0x6a4, 2, 0);
	for (i = 0; i < 6; i++) {
		WRITE(0x610 + i, 1, cfg.mac[i]);
		WRITE(0x618 + i, 1, cfg.bssid[i]);
	}
	MASK(0x6a8, 0x7ff, cfg.aid);
	MASK(0x440, 0xfff, cfg.basic_rates);
	WRITE(0x51b, 1, cfg.slot);
	if (rtw8723d_station_live(io, check, cookie))
		rtw88_io_set(io, 0x550, 1, 0x18);
	if (rtw8723d_station_live(io, check, cookie))
		rtw88_io_clr(io, 0x608, 4, 0xc0);
	media[0] = cfg.linked ? 1 : 0;
	if (rtw8723d_station_live(io, check, cookie)) {
		error = rtw8723d_mailbox_send(io, box, generation, 0x01,
		    media, sizeof(media), check, cookie);
		if (error != 0 && io->error == 0)
			io->error = error;
	}
	if (cfg.linked) {
		if (rtw8723d_station_live(io, check, cookie))
			rtw88_io_set(io, 0x608, 4, 0xc0);
		if (rtw8723d_station_live(io, check, cookie))
			rtw88_io_clr(io, 0x550, 1, 0x10);
		WRITE(0x6a4, 2, 0xffff);
		MASK(0x100, 0x30000, 2);
		if (rtw8723d_station_live(io, check, cookie))
			rtw88_io_clr(io, 0x522, 1, 0x0f);
	}
	(void)rtw8723d_station_live(io, check, cookie);
	if (io->error != 0) {
		error = io->error;
		/* Independent bounded attempts; never reopen on an error. */
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr_mask(&cleanup, 0x100, 0x30000, 0);
		rtw88_io_init(&cleanup, io->ops, io->cookie);
		rtw88_io_wr(&cleanup, 0x6a4, 2, 0);
		io->error = error;
	}
#undef MASK
#undef WRITE
	return io->error;
}
