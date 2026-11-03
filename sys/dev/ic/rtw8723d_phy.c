/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2018-2019 Realtek Corporation. */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: Linux adc218676eef25575469234709c2d87185ca223a rtw88/phy.c;
 * native validation and fallible table application are AI-assisted. */
#include <dev/ic/rtw8723d_phy.h>
#include <dev/ic/rtw8723d_rf.h>
#include <dev/ic/rtw88_io.h>
#include <dev/ic/rtw88_phy_table.h>

struct rtw8723d_phy_context {
	struct rtw88_io *io;
	struct rtw8723d_phy_condition condition;
	struct rtw8723d_phy_image image;
	int (*check)(void *);
	void *cookie;
};

static int
rtw8723d_phy_prepare(struct rtw88_io *io,
    const struct rtw8723d_phy_condition *condition)
{
	struct rtw88_io validation;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (condition == NULL || condition->package > 15 ||
	    (condition->interface != RTW8723D_PHY_PCIE &&
	     condition->interface != RTW8723D_PHY_USB &&
	     condition->interface != RTW8723D_PHY_SDIO))
		return io->error = EINVAL;
	/* Match the supported bootstrap cuts and the one pinned RFE table. */
	if (condition->cut > 6 || condition->rfe != 0)
		return io->error = EOPNOTSUPP;
	rtw88_io_init(&validation, io->ops, io->cookie);
	return io->error = validation.error;
}

static int
rtw8723d_phy_validate_image(const struct rtw8723d_phy_image *image)
{
	size_t i;
	uint32_t addr, data;
	int error;

	if (image == NULL || image->data == NULL || image->count == 0 ||
	    image->rf_path != 0)
		return EINVAL;
	if (image->space != RTW8723D_PHY_MAC &&
	    image->space != RTW8723D_PHY_AGC &&
	    image->space != RTW8723D_PHY_BB &&
	    image->space != RTW8723D_PHY_RF)
		return EINVAL;
	error = rtw88_phy_table_validate(image->data, image->count);
	if (error != 0)
		return error;
	for (i = 0; i < image->count; i += 2) {
		addr = image->data[i];
		data = image->data[i + 1];
		if ((addr & 0xc0000000U) != 0)
			continue;
		if (image->space == RTW8723D_PHY_RF) {
			if (addr > 0xff && addr != 0xffe)
				return EINVAL;
		} else if (image->space == RTW8723D_PHY_MAC) {
			if (addr > 0xffff || data > 0xff)
				return EINVAL;
		} else {
			if (image->space == RTW8723D_PHY_BB &&
			    addr >= 0xf9 && addr <= 0xfe)
				continue;
			if (addr > 0xffff || (addr & 3) != 0)
				return EINVAL;
		}
	}
	return 0;
}

static int
rtw8723d_phy_check(void *cookie)
{
	struct rtw8723d_phy_context *ctx = cookie;

	if (ctx->io->error != 0)
		return ctx->io->error;
	if (ctx->check != NULL)
		ctx->io->error = ctx->check(ctx->cookie);
	return ctx->io->error;
}

static int
rtw8723d_phy_match(void *cookie, uint32_t word, int *matched)
{
	struct rtw8723d_phy_context *ctx = cookie;
	const struct rtw8723d_phy_condition *c = &ctx->condition;
	unsigned cut = (word >> 24) & 15;
	unsigned package = (word >> 12) & 15;
	unsigned interface = (word >> 8) & 15;
	unsigned driver_cut = c->cut != 0 ? c->cut : 15;
	unsigned driver_package = c->package != 0 ? c->package : 15;

	/* The pinned donor matches these four fields; platform/reserved bits
	 * are not predicates. Never replace the actual HCI selector with PCIe. */
	*matched = (cut == 0 || cut == driver_cut) &&
	    (package == 0 || package == driver_package) &&
	    (interface == 0 || interface == c->interface) &&
	    (word & 0xff) == c->rfe;
	return 0;
}

static int
rtw8723d_phy_write(void *cookie, uint32_t addr, uint32_t data)
{
	struct rtw8723d_phy_context *ctx = cookie;
	struct rtw88_io *io = ctx->io;
	unsigned delay;

	switch (ctx->image.space) {
	case RTW8723D_PHY_MAC:
		rtw88_io_wr(io, addr, 1, data);
		break;
	case RTW8723D_PHY_AGC:
		rtw88_io_wr(io, addr, 4, data);
		break;
	case RTW8723D_PHY_BB:
		switch (addr) {
		case 0xfe: delay = 50000; break;
		case 0xfd: delay = 5000; break;
		case 0xfc: delay = 1000; break;
		case 0xfb: delay = 50; break;
		case 0xfa: delay = 5; break;
		case 0xf9: delay = 1; break;
		default: delay = 0; break;
		}
		if (delay != 0)
			rtw88_io_delay(io, delay);
		else
			rtw88_io_wr(io, addr, 4, data);
		break;
	case RTW8723D_PHY_RF:
		if (addr == 0xffe)
			rtw88_io_delay(io, 50000);
		else if (addr == 0xfe)
			rtw88_io_delay(io, 100);
		else {
			(void)rtw8723d_rf_write(io, ctx->image.rf_path, addr,
			    0xfffff, data);
			rtw88_io_delay(io, 1);
		}
		break;
	default:
		return io->error = EINVAL;
	}
	return io->error;
}

int
rtw8723d_phy_apply_table(struct rtw88_io *io,
    const struct rtw8723d_phy_condition *condition,
    const struct rtw8723d_phy_image *image, int (*check)(void *), void *cookie)
{
	static const struct rtw88_phy_table_ops ops = {
		.match = rtw8723d_phy_match,
		.write = rtw8723d_phy_write,
		.check = rtw8723d_phy_check
	};
	struct rtw8723d_phy_context ctx;
	int error;

	error = rtw8723d_phy_prepare(io, condition);
	if (error != 0)
		return error;
	error = rtw8723d_phy_validate_image(image);
	if (error != 0)
		return io->error = error;
	ctx.io = io;
	ctx.condition = *condition;
	ctx.image = *image;
	ctx.check = check;
	ctx.cookie = cookie;
	error = rtw88_phy_table_apply(ctx.image.data, ctx.image.count, &ops, &ctx);
	if (io->error == 0)
		io->error = error;
	return io->error;
}

int
rtw8723d_phy_load_tables(struct rtw88_io *io,
    const struct rtw8723d_phy_condition *condition,
    int (*check)(void *), void *cookie)
{
	static const struct rtw8723d_phy_image * const images[] = {
		&rtw8723d_phy_mac, &rtw8723d_phy_bb,
		&rtw8723d_phy_agc, &rtw8723d_phy_rf_a
	};
	struct rtw8723d_phy_condition snapshot;
	size_t i;
	int error;

	error = rtw8723d_phy_prepare(io, condition);
	if (error != 0)
		return error;
	snapshot = *condition;
	for (i = 0; i < sizeof(images) / sizeof(images[0]); i++) {
		error = rtw8723d_phy_validate_image(images[i]);
		if (error != 0)
			return io->error = error;
	}
	for (i = 0; i < sizeof(images) / sizeof(images[0]); i++) {
		error = rtw8723d_phy_apply_table(io, &snapshot, images[i], check, cookie);
		if (error != 0)
			return error;
	}
	return 0;
}

/* Scalar register facts from pinned rtw8723d.c:phy_set_param and reg.h.
 * Preserve access widths, including the donor's byte TBTT_PROHIBIT write.
 * mask == 0 denotes a full write; otherwise preserve all other bits. */
struct rtw8723d_phy_step {
	uint32_t address, mask, value;
	unsigned width, read_width;
};

static int
rtw8723d_phy_steps(struct rtw88_io *io,
    const struct rtw8723d_phy_step *steps, size_t count,
    int (*check)(void *), void *cookie)
{
	size_t i;
	uint32_t value;

	for (i = 0; i < count; i++) {
		if (io->error == 0 && check != NULL)
			io->error = check(cookie);
		if (io->error != 0)
			break;
		value = steps[i].value;
		if (steps[i].mask != 0)
			value |= rtw88_io_rd(io, steps[i].address,
			    steps[i].read_width != 0 ? steps[i].read_width :
			    steps[i].width) & ~steps[i].mask;
		rtw88_io_wr(io, steps[i].address, steps[i].width, value);
	}
	return io->error;
}

int
rtw8723d_phy_configure(struct rtw88_io *io,
    const struct rtw8723d_phy_condition *condition, uint8_t crystal_cap,
    uint8_t afe, int (*check)(void *), void *cookie)
{
	static const struct rtw8723d_phy_step before[] = {
		{ 0x0002, 0x2003, 0x2003, 2, 0 },
		{ 0x001f, 0x0007, 0x0007, 1, 0 },
		{ 0x0025, 0, 0x80, 1, 0 }
	};
	static const struct rtw8723d_phy_step special_afe[] = {
		{ 0x002c, 0x10000000, 0x10000000, 4, 0 },
		{ 0x0024, 0x000000f0, 0, 4, 0 },
		{ 0x007c, 0x20000000, 0x20000000, 4, 0 },
		{ 0x007c, 0x10000000, 0, 4, 0 }
	};
	static const struct rtw8723d_phy_step timing[] = {
		{ 0x051b, 0, 0x09, 1, 0 },
		{ 0x0421, 0, 0x1f, 1, 0 },
		{ 0x042a, 0, 0x3030, 2, 0 },
		{ 0x04cc, 0, 0x0201ffff, 4, 0 },
		{ 0x055a, 0, 0x02, 1, 0 },
		{ 0x0550, 0, 0x1c, 1, 0 },
		{ 0x0540, 0x000fffff, 0x6404, 1, 4 },
		{ 0x0512, 0, 0, 1, 0 },
		{ 0x051a, 0, 0x16, 1, 0 },
		{ 0x0546, 0, 0x0040, 2, 0 },
		{ 0x063a, 0, 0x100a, 2, 0 },
		{ 0x0514, 0, 0x100a, 2, 0 },
		{ 0x0516, 0, 0x100a, 2, 0 },
		{ 0x04c7, 0, 0x80, 1, 0 },
		{ 0x060c, 0, 0x17, 1, 0 },
		{ 0x04ca, 0, 0x0a, 1, 0 },
		{ 0x0456, 0, 0x1c, 1, 0 },
		{ 0x004e, 0, 0x82, 1, 0 },
		{ 0x0798, 0, 0x90039003, 4, 0 },
		{ 0x079c, 0, 0x883c883c, 4, 0 },
		{ 0x07a4, 0, 0xcb004010, 4, 0 },
		{ 0x07a8, 0, 0x01233425, 4, 0 }
	};
	struct rtw8723d_phy_step after[] = {
		{ 0x0608, 0x00000800, 0, 4, 0 },
		{ 0x05a7, 0x00000001, 0x00000001, 1, 0 },
		{ 0x0078, 0x00008010, 0x00008010, 2, 0 },
		{ 0x002c, 0x00fff000, 0, 4, 0 },
		{ 0x0800, 0x03000000, 0x03000000, 4, 0 }
	};
	const struct rtw8723d_phy_image * const images[] = {
		&rtw8723d_phy_mac, &rtw8723d_phy_bb,
		&rtw8723d_phy_agc, &rtw8723d_phy_rf_a
	};
	struct rtw8723d_phy_condition snapshot;
	size_t i;
	int error;

	error = rtw8723d_phy_prepare(io, condition);
	if (error != 0)
		return error;
	if (crystal_cap > 63)
		return io->error = EINVAL;
	snapshot = *condition;
	/* Preflight every image before enabling any part of the BB/RF domain. */
	for (i = 0; i < sizeof(images) / sizeof(images[0]); i++) {
		error = rtw8723d_phy_validate_image(images[i]);
		if (error != 0)
			return io->error = error;
	}
	after[3].value = ((uint32_t)crystal_cap |
	    ((uint32_t)crystal_cap << 6)) << 12;
	(void)rtw8723d_phy_steps(io, before, sizeof(before) / sizeof(before[0]),
	    check, cookie);
	if (io->error == 0)
		(void)rtw8723d_phy_load_tables(io, &snapshot, check, cookie);
	(void)rtw8723d_phy_steps(io, after, sizeof(after) / sizeof(after[0]),
	    check, cookie);
	if ((afe >> 4) == 14)
		(void)rtw8723d_phy_steps(io, special_afe,
		    sizeof(special_afe) / sizeof(special_afe[0]), check, cookie);
	(void)rtw8723d_phy_steps(io, timing, sizeof(timing) / sizeof(timing[0]),
	    check, cookie);
	return io->error;
}
