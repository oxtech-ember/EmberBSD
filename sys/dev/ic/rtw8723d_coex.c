/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* Original native minimum always-awake policy, AI-assisted. Protocol facts:
 * dual-BSD Linux adc218676eef25575469234709c2d87185ca223a rtw88/coex.c,
 * fw.c/fw.h, util.c, rtw8723d.c and rtw8723x.c/h; source notes and
 * exact blobs are in the accompanying donor protocol evidence. Native RMW
 * aborts on a failed read instead of writing a value computed from zero.
 * No Linux implementation body or complete adaptive BT engine is embedded. */
#include <dev/ic/rtw8723d_coex.h>
#include <dev/ic/rtw8723d_chip.h>
#include <dev/ic/rtw8723d_runtime.h>
#include <dev/ic/rtw88_io.h>

#define RTW8723D_COEX_READY UINT32_C(0x20000000)

static bool
rtw8723d_coex_live(struct rtw88_io *io, struct rtw8723d_runtime *ctx)
{

	if (io->error == 0)
		io->error = rtw8723d_runtime_check(ctx);
	return io->error == 0;
}

static uint32_t
rtw8723d_coex_read(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    uint32_t address, unsigned width)
{
	uint32_t value = 0;

	if (rtw8723d_coex_live(io, ctx))
		value = rtw88_io_rd(io, address, width);
	(void)rtw8723d_coex_live(io, ctx);
	return value;
}

static void
rtw8723d_coex_write(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    uint32_t address, unsigned width, uint32_t value)
{

	if (rtw8723d_coex_live(io, ctx))
		rtw88_io_wr(io, address, width, value);
	(void)rtw8723d_coex_live(io, ctx);
}

static void
rtw8723d_coex_field(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    uint32_t address, unsigned width, uint32_t mask, uint32_t data)
{
	uint32_t value;

	if (mask == 0) {
		if (io->error == 0)
			io->error = EINVAL;
		return;
	}
	value = rtw8723d_coex_read(io, ctx, address, width);
	rtw8723d_coex_write(io, ctx, address, width,
	    (value & ~mask) | ((data << __builtin_ctz(mask)) & mask));
}

static void
rtw8723d_coex_lte_ready(struct rtw88_io *io, struct rtw8723d_runtime *ctx)
{
	unsigned i;
	uint32_t value;

	/* util.c check_hw_ready:1000 polls,10us after every failed poll. */
	for (i = 0; i < 1000 && rtw8723d_coex_live(io, ctx); i++) {
		value = rtw8723d_coex_read(io, ctx, 0x7c0, 4);
		if (io->error != 0 || (value & RTW8723D_COEX_READY) != 0)
			return;
		rtw88_io_delay(io, 10);
	}
	if (io->error == 0)
		io->error = ETIMEDOUT;
}

static void
rtw8723d_coex_lte_field(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    uint16_t offset, uint32_t mask, uint32_t data)
{
	uint32_t value;

	if (mask == 0) {
		if (io->error == 0)
			io->error = EINVAL;
		return;
	}
	rtw8723d_coex_lte_ready(io, ctx);
	rtw8723d_coex_write(io, ctx, 0x7c0, 4, UINT32_C(0x800f0000) | offset);
	value = rtw8723d_coex_read(io, ctx, 0x7c8, 4);
	if (io->error != 0)
		return; /* Never derive a write from a failed indirect read. */
	rtw8723d_coex_lte_ready(io, ctx);
	value = (value & ~mask) | ((data << __builtin_ctz(mask)) & mask);
	rtw8723d_coex_write(io, ctx, 0x7c4, 4, value);
	rtw8723d_coex_write(io, ctx, 0x7c0, 4, UINT32_C(0xc00f0000) | offset);
}

static void
rtw8723d_coex_rfk_wait(struct rtw88_io *io, struct rtw8723d_runtime *ctx)
{
	unsigned i;
	uint32_t bt, wl;

	/* coex.c avoids changing the path owner during BT or WLAN RFK.
	 * Unlike the void donor, exhaustion is a hard error, never success. */
	for (i = 0; i < 60 && rtw8723d_coex_live(io, ctx); i++) {
		bt = rtw8723d_coex_read(io, ctx, 0xaa, 2);
		wl = rtw8723d_coex_read(io, ctx, 0x49c, 1);
		if (io->error != 0 || ((bt & 0x20) == 0 && (wl & 1) == 0))
			return;
		ctx->boot.ops.wait_ms(ctx->boot.ops.cookie, 10);
	}
	if (io->error == 0)
		io->error = ETIMEDOUT;
}

static void
rtw8723d_coex_rfe(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    bool shared, bool aux)
{
	uint32_t select = shared ? (aux ? 0x80 : 0x200) : (aux ? 0x280 : 0);

	rtw8723d_coex_write(io, ctx, 0x948, 2, select);
	rtw8723d_coex_lte_field(io, ctx, 0x38, 0x80, 0);
	rtw8723d_coex_lte_field(io, ctx, 0xa0, 0xffff, 0xffff);
	rtw8723d_coex_lte_field(io, ctx, 0xa4, 0xffff, 0xffff);
}

static void
rtw8723d_coex_owner(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    bool wlan)
{

	rtw8723d_coex_field(io, ctx, 0x73, 1, 4, wlan ? 1 : 0);
	rtw8723d_coex_field(io, ctx, 0x67, 1, 0x80, wlan ? 1 : 0);
}

static void
rtw8723d_coex_grants(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    unsigned bt, unsigned wl)
{

	rtw8723d_coex_lte_field(io, ctx, 0x38, 0xc000, bt);
	rtw8723d_coex_lte_field(io, ctx, 0x38, 0x0c00, bt);
	rtw8723d_coex_lte_field(io, ctx, 0x38, 0x3000, wl);
	rtw8723d_coex_lte_field(io, ctx, 0x38, 0x0300, wl);
}

static void
rtw8723d_coex_table(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    uint32_t table)
{

	rtw8723d_coex_write(io, ctx, 0x6c0, 4, table);
	rtw8723d_coex_write(io, ctx, 0x6c4, 4, table);
	rtw8723d_coex_write(io, ctx, 0x6c8, 4, UINT32_C(0xf0ffffff));
}

static void
rtw8723d_coex_command(struct rtw88_io *io, struct rtw8723d_runtime *ctx,
    uint8_t command, const uint8_t *data, size_t size)
{

	if (rtw8723d_coex_live(io, ctx))
		io->error = rtw8723d_runtime_command(ctx->boot.core,
		    command, data, size);
}

int
rtw8723d_coex_start(struct rtw8723d_runtime *ctx)
{
	static const uint8_t slot[6] = { 0x0c, 1, 0, 0, 0, 0 };
	static const uint8_t query[1] = { 1 };
	static const struct { uint16_t address; uint8_t mask; } debug[] = {
		{ 0x4e, 0x40 }, { 0x67, 1 }, { 0x4a, 0x10 }, { 0x42, 2 },
		{ 0x67, 2 }, { 0x66, 0x80 }, { 0x09, 2 }, { 0x73, 8 }
	};
	struct rtw88_softc *sc;
	struct rtw88_io io, cleanup;
	struct rtw8723d_efuse efuse;
	uint8_t tdma[5] = {0};
	uint32_t table;
	bool shared, aux, bt_support, bt_enabled;
	unsigned i;
	int error;

	if (ctx == NULL || (sc = ctx->boot.core) == NULL ||
	    sc->sc_chip != &rtw8723d_chip_ops || ctx->boot.ops.wait_ms == NULL)
		return EINVAL;
	if ((error = rtw8723d_runtime_check(ctx)) != 0)
		return error;
	if (!sc->sc_bootstrapping || sc->sc_started || sc->sc_rf_enabled ||
	    !sc->sc_running || !ctx->phy.configured ||
	    ctx->phy.generation != sc->sc_generation ||
	    ctx->mailbox.active || ctx->mailbox.uncertain || ctx->mailbox.error ||
	    ctx->mailbox.generation != sc->sc_generation || ctx->mailbox.next > 3)
		return EBUSY;
	efuse = ctx->identity.efuse;
	if (efuse.rfe != 0)
		return EOPNOTSUPP;
	shared = (efuse.bt_setting & 1) != 0;
	aux = (efuse.bt_setting & 0x40) != 0;
	bt_support = efuse.board_option != 0xff &&
	    (efuse.board_option & 0xe0) == 0x20;
	/* Nonshared baseline is the documented not-connected case100, not an
	 * underflowed index from the donor's unconditional init case1/TDMA0. */
	table = shared ? UINT32_C(0x55555555) : UINT32_MAX;
	if (!shared)
		tdma[4] = 1;
	rtw88_io_init(&io, sc->sc_ops, sc->sc_bus);
	if (rtw8723d_coex_read(&io, ctx, 0x522, 1) != 0xff)
		return io.error != 0 ? io.error : EBUSY;
	/* Power-on: BB accessible, current BT status, RFE, owner, baseline table,
	 * then release debug GPIO muxes. Check RFK before the first path write. */
	rtw8723d_coex_field(&io, ctx, 0x02, 1, 3, 3);
	bt_enabled = (rtw8723d_coex_read(&io, ctx, 0xaa, 2) & 2) != 0;
	rtw8723d_coex_rfk_wait(&io, ctx);
	rtw8723d_coex_rfe(&io, ctx, shared, aux);
	rtw8723d_coex_owner(&io, ctx, !bt_enabled);
	rtw8723d_coex_table(&io, ctx, table);
	rtw8723d_coex_write(&io, ctx, 0xff1a, 1, 0);
	for (i = 0; i < sizeof(debug) / sizeof(debug[0]); i++)
		rtw8723d_coex_field(&io, ctx, debug[i].address, 1, debug[i].mask, 0);
	/* Hardware init, fresh firmware has never entered LPS: no invented
	 * packet H2C or power-save transition is required for TDMA-off. */
	bt_enabled = (rtw8723d_coex_read(&io, ctx, 0xaa, 2) & 2) != 0;
	rtw8723d_coex_command(&io, ctx, 0x69, slot, sizeof(slot));
	rtw8723d_coex_field(&io, ctx, 0x550, 1, 8, 1);
	rtw8723d_coex_rfk_wait(&io, ctx);
	rtw8723d_coex_rfe(&io, ctx, shared, aux);
	rtw8723d_coex_field(&io, ctx, 0x550, 1, 8, 1);
	rtw8723d_coex_field(&io, ctx, 0x790, 1, 0x3f, 5);
	rtw8723d_coex_write(&io, ctx, 0x778, 1, 1);
	rtw8723d_coex_field(&io, ctx, 0x40, 4, UINT32_C(1) << 5, 1);
	rtw8723d_coex_field(&io, ctx, 0x40, 4, UINT32_C(1) << 9, 1);
	rtw8723d_coex_field(&io, ctx, 0x4c6, 1, 0x10, 1);
	rtw8723d_coex_field(&io, ctx, 0x6cc, 1, 8, 1);
	rtw8723d_coex_field(&io, ctx, 0x6cc, 1, 0x10, 1);
	rtw8723d_coex_field(&io, ctx, 0x6cf, 1, 8, 1);
	rtw8723d_coex_rfk_wait(&io, ctx);
	/* INIT uses BT SW-high/WL SW-low only while BT is active. WONLY is
	 * selected by the real board support flag, never by a convenience mode. */
	if (bt_support && bt_enabled)
		rtw8723d_coex_grants(&io, ctx, 3, 1);
	else
		rtw8723d_coex_grants(&io, ctx, 1, 3);
	rtw8723d_coex_owner(&io, ctx, true);
	rtw8723d_coex_write(&io, ctx, 0xaa, 2, 0x8003);
	rtw8723d_coex_table(&io, ctx, table);
	rtw8723d_coex_command(&io, ctx, 0x60, tdma, sizeof(tdma));
	if (bt_enabled)
		rtw8723d_coex_command(&io, ctx, 0x61, query, sizeof(query));
	/* Do not leave BT-capable WLAN blocked in INIT until a missing scan
	 * callback. Apply the selected 2G phase now, before any RF admission.
	 * Keep fixed baseline arbitration; dynamic BT profile adaptation is
	 * deliberately outside this initial always-awake station policy. */
	if (bt_support) {
		rtw8723d_coex_rfk_wait(&io, ctx);
		rtw8723d_coex_grants(&io, ctx, 0, 0);
		rtw8723d_coex_owner(&io, ctx, true);
		if (shared && !bt_enabled)
			rtw8723d_coex_table(&io, ctx, UINT32_C(0x66555555));
	}
	(void)rtw8723d_coex_live(&io, ctx);
	if (io.error == 0)
		return 0;
	error = io.error;
	ctx->boot.failed = true;
	sc->sc_rf_enabled = 0;
	sc->sc_phy.channel_valid = 0;
	if (sc->sc_resources_ready && sc->sc_powered && ctx->boot.owned) {
		rtw88_io_init(&cleanup, sc->sc_ops, sc->sc_bus);
		rtw88_io_wr(&cleanup, 0x522, 1, 0xff);
	}
	return error;
}
