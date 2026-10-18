/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Fixed VC6 HDMI0 register facts: Raspberry Pi Linux
 * a1a601d4857978d810175365550c4536701df4ed, vc4_hdmi_phy.c and vc4_hdmi_regs.h.
 * Independent BSD mechanism. No Linux implementation code is copied.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display_phyvar.h>

/* Fixed programming facts, not a caller-supplied register language. */
struct bcm2712_phy_setting {
	enum bcm2712_phy_window window;
	unsigned int offset;
	uint32_t value;
};
static const struct bcm2712_phy_setting bcm2712_phy_148500[] = {
	{ BCM2712_PHY_WINDOW, 0x000, 0 },
	{ BCM2712_PHY_WINDOW, 0x004, 0 },
	{ BCM2712_PHY_WINDOW, 0x028, 0x10 },
	{ BCM2712_PHY_WINDOW, 0x060, 0x810c6000 },
	{ BCM2712_PHY_WINDOW, 0x064, 0x00b8c451 },
	{ BCM2712_PHY_WINDOW, 0x068, 0x46402e31 },
	{ BCM2712_PHY_WINDOW, 0x06c, 0x00b8c005 },
	{ BCM2712_PHY_WINDOW, 0x070, 0x42410261 },
	{ BCM2712_PHY_WINDOW, 0x074, 0xcc021001 },
	{ BCM2712_PHY_WINDOW, 0x078, 0xc8301c80 },
	{ BCM2712_PHY_WINDOW, 0x07c, 0xb0804444 },
	{ BCM2712_PHY_WINDOW, 0x080, 0xf80f8000 },
	{ BCM2712_PHY_WINDOW, 0x01c, 0x2036 },
	{ BCM2712_PHY_WINDOW, 0x000, 0x7f },
	{ BCM2712_RM_WINDOW, 0x018, 0x98100000 },
	{ BCM2712_PHY_WINDOW, 0x02c, 0x407 },
	{ BCM2712_PHY_WINDOW, 0x044, 0 },
	{ BCM2712_PHY_WINDOW, 0x028, 0x9 },
	{ BCM2712_PHY_WINDOW, 0x008, 0x80828700 },
	{ BCM2712_PHY_WINDOW, 0x00c, 0x80828700 },
	{ BCM2712_PHY_WINDOW, 0x010, 0x80828700 },
	{ BCM2712_PHY_WINDOW, 0x014, 0x80828700 },
	{ BCM2712_PHY_WINDOW, 0x054, 0 },
	{ BCM2712_PHY_WINDOW, 0x004, 0x1cf },
	{ BCM2712_PHY_WINDOW, 0x194, 0x1 }
};

int
bcm2712_phy_init(struct bcm2712_phy *out, const struct bcm2712_phy_io *io)
{
	struct bcm2712_phy result = { 0 };
	struct bcm2712_phy_io copy;

	if (out == NULL || io == NULL)
		return EINVAL;
	copy = *io;
	if (out->initialized)
		return EBUSY;
	if (copy.read == NULL || copy.write == NULL || copy.barrier == NULL)
		return EINVAL;
	result.io = copy;
	result.initialized = true;
	*out = result;
	return 0;
}

static int
bcm2712_phy_ready(const struct bcm2712_phy *sc)
{

	if (sc == NULL)
		return EINVAL;
	if (!sc->initialized)
		return ENXIO;
	return sc->status.fault;
}

static int
bcm2712_phy_write(struct bcm2712_phy *sc, enum bcm2712_phy_window window,
    unsigned int offset, uint32_t value)
{
	int error;

	sc->status.step++;
	sc->status.mutation_attempted = true;
	error = sc->io.write(sc->io.arg, window, offset, value);
	if (error != 0) {
		sc->status.fault = error;
		return error;
	}
	sc->status.writes++;
	sc->io.barrier(sc->io.arg);
	return 0;
}

static int
bcm2712_phy_reset_bit(struct bcm2712_phy *sc, bool release)
{
	uint32_t value;
	int error;

	sc->status.step++;
	error = sc->io.read(sc->io.arg, BCM2712_PHY_WINDOW, 0x190, &value);
	if (error != 0) {
		sc->status.fault = error;
		return error;
	}
	sc->status.reads++;
	value = release ? value | 1U : value & ~1U;
	return bcm2712_phy_write(sc, BCM2712_PHY_WINDOW, 0x190, value);
}

int
bcm2712_phy_program_148500(struct bcm2712_phy *sc)
{
	const struct bcm2712_phy_setting *setting;
	unsigned int i;
	int error = bcm2712_phy_ready(sc);

	if (error != 0)
		return error;
	if (sc->status.stage != BCM2712_PHY_IDLE)
		return EBUSY;
	sc->status.stage = BCM2712_PHY_PROGRAM;
	for (i = 0; i < __arraycount(bcm2712_phy_148500); i++) {
		setting = &bcm2712_phy_148500[i];
		error = bcm2712_phy_write(sc, setting->window, setting->offset,
		    setting->value);
		if (error != 0)
			return error;
	}
	error = bcm2712_phy_reset_bit(sc, false);
	if (error != 0)
		return error;
	error = bcm2712_phy_reset_bit(sc, true);
	if (error != 0)
		return error;
	sc->status.stage = BCM2712_PHY_PROGRAMMED;
	sc->status.programming_complete = true;
	return 0;
}

int
bcm2712_phy_disable(struct bcm2712_phy *sc)
{
	unsigned int i;
	int error = bcm2712_phy_ready(sc);

	if (error != 0)
		return error;
	if (sc->status.stage != BCM2712_PHY_PROGRAMMED)
		return EBUSY;
	sc->status.stage = BCM2712_PHY_DISABLE;
	sc->status.step = 0;
	sc->status.programming_complete = false;
	for (i = 0; i < 3; i++) {
		error = bcm2712_phy_write(sc, bcm2712_phy_148500[i].window,
		    bcm2712_phy_148500[i].offset, bcm2712_phy_148500[i].value);
		if (error != 0)
			return error;
	}
	sc->status.stage = BCM2712_PHY_DISABLED;
	sc->status.disable_complete = true;
	return 0;
}

int
bcm2712_phy_status(const struct bcm2712_phy *sc,
    struct bcm2712_phy_status *out)
{

	if (sc == NULL || out == NULL)
		return EINVAL;
	if (!sc->initialized)
		return ENXIO;
	*out = sc->status;
	return 0;
}
