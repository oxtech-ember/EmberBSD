/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted independent A733 SNPS C422-0 control.
 * Register facts and fixed table values: the pinned BSP sources identified
 * in ember/boot/a733-hdmi-phy.md. Do not use the older C082 PHY recipe.
 */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#endif
#include "sun60i_a733_hdmi_phy.h"

#define TOP_CONTROL	0x00
#define TOP_PAD		0x04
#define TOP_PLL		0x20
#define TOP_CLOCK	0x24
#define TOP_LDO		0x28
#define TOP_PATTERN0	0x2c
#define TOP_PATTERN1	0x30
#define TOP_LOCK	0x40
#define PLL_FIELDS	UINT32_C(0x007fffe2)
#define PLL_ENABLE	UINT32_C(0xe0000000)
#define PLL_OUTPUT	UINT32_C(0x08000000)
#define CORE_PHY_CONF	0x3000
#define CORE_PHY_STATUS	0x3004
#define CORE_PHY_MASK	0x3006
#define CORE_PHY_I2C_STATUS 0x0108
#define CORE_PHY_I2C_SLAVE 0x3020
#define CORE_PHY_I2C_ADDR 0x3021
#define CORE_PHY_I2C_OUT_HI 0x3022
#define CORE_PHY_I2C_OUT_LO 0x3023
#define CORE_PHY_I2C_IN_HI 0x3024
#define CORE_PHY_I2C_IN_LO 0x3025
#define CORE_PHY_I2C_OP	0x3026
#define CORE_PHY_I2C_INT	0x3027
#define CORE_PHY_I2C_CTLINT 0x3028
#define CORE_PHY_JTAG	0x3034
#define CORE_PHY_RESET	0x4005

/*
 * Admit only exact integer outputs from the platform's fixed table.
 * The 26 MHz entries for 27/74.25/148.5 MHz do not encode those rates.
 * Do not substitute the 24 MHz normalized system clock for physical DCXO.
 */
static const struct sun60i_a733_hdmi_plan plans[] = {
	{ 24000000, 27000000, 0xe8576200, 0x80000000,
	    { 0x0003, 0x0283, 0x0628 } },
	{ 24000000, 54000000, 0xe82b6200, 0x80000000,
	    { 0x0002, 0x1183, 0x0694 } },
	{ 24000000, 74250000, 0xe81f6200, 0x80000000,
	    { 0x0002, 0x1142, 0x0214 } },
	{ 24000000, 148500000, 0xe80f6200, 0x80000000,
	    { 0x0001, 0x2080, 0x020a } },
	{ 26000000, 54000000, 0xe80c1a00, 0x80000000,
	    { 0x0002, 0x1183, 0x0694 } },
	{ 26000000, 108000000, 0xe80c3500, 0x00000000,
	    { 0x0001, 0x20c0, 0x060a } }
};

int
sun60i_a733_hdmi_plan(uint32_t dcxo, uint32_t pixel,
    struct sun60i_a733_hdmi_plan *out)
{
	uint64_t numerator, denominator;
	size_t i;

	if (out == NULL)
		return EINVAL;
	for (i = 0; i < sizeof(plans) / sizeof(plans[0]); i++) {
		const struct sun60i_a733_hdmi_plan *p = &plans[i];
		if (p->dcxo_hz != dcxo || p->pixel_hz != pixel)
			continue;
		numerator = (uint64_t)dcxo * (((p->pll >> 8) & 255) + 1);
		denominator = (uint64_t)(((p->pll >> 1) & 1) + 1) *
		    (((p->pll >> 16) & 127) + 1);
		if (numerator != (uint64_t)pixel * denominator)
			return EOPNOTSUPP;
		*out = *p;
		return 0;
	}
	return EOPNOTSUPP;
}

static int
phy_fail(struct sun60i_a733_hdmi_phy *s, int error)
{
	if (s->fault == 0 && error != 0)
		s->fault = error;
	return s->fault;
}

static int
phy_ready(struct sun60i_a733_hdmi_phy *s)
{
	if (s == NULL)
		return EINVAL;
	if (s->fault != 0)
		return s->fault;
	if (s->io == NULL || s->owner == NULL ||
	    s->io->ready == NULL || s->io->read == NULL ||
	    s->io->write == NULL || s->io->delay == NULL)
		return phy_fail(s, EINVAL);
	return phy_fail(s, s->io->ready(s->io->arg, s->owner));
}

static uint32_t
phy_read(struct sun60i_a733_hdmi_phy *s,
    enum sun60i_a733_hdmi_space space, uint32_t reg)
{
	uint32_t value = 0;
	int error;

	if (s->fault != 0)
		return 0;
	error = s->io->read(s->io->arg, space, reg, &value);
	if (error != 0) {
		phy_fail(s, error);
		return 0;
	}
	return value;
}

static void
phy_write(struct sun60i_a733_hdmi_phy *s,
    enum sun60i_a733_hdmi_space space, uint32_t reg, uint32_t value)
{
	if (s->fault != 0)
		return;
	/* The entry point proved an already retained native lease. */
	s->written = true;
	phy_fail(s, s->io->write(s->io->arg, space, reg, value));
}

static void
phy_delay(struct sun60i_a733_hdmi_phy *s, unsigned us)
{
	if (s->fault == 0)
		s->io->delay(s->io->arg, us);
}

static void
phy_update(struct sun60i_a733_hdmi_phy *s,
    enum sun60i_a733_hdmi_space space, uint32_t reg,
    uint32_t mask, uint32_t bits)
{
	uint32_t before, after;

	before = phy_read(s, space, reg);
	phy_write(s, space, reg, (before & ~mask) | bits);
	after = phy_read(s, space, reg);
	if (s->fault == 0 && (after & mask) != bits)
		phy_fail(s, EIO);
}

static void
phy_wait(struct sun60i_a733_hdmi_phy *s,
    enum sun60i_a733_hdmi_space space, uint32_t reg,
    uint32_t mask, uint32_t bits, unsigned count, unsigned us)
{
	unsigned i;

	for (i = 0; i < count && s->fault == 0; i++) {
		phy_delay(s, us);
		if ((phy_read(s, space, reg) & mask) == bits && s->fault == 0)
			return;
	}
	phy_fail(s, ETIMEDOUT);
}

int
sun60i_a733_hdmi_hpd_prepare(struct sun60i_a733_hdmi_phy *s)
{
	uint32_t control;
	int error;

	if ((error = phy_ready(s)) != 0)
		return error;
	if (s->hpd_prepared || s->phy_prepared)
		return phy_fail(s, EALREADY);
	control = phy_read(s, A733_HDMI_TOP32, TOP_CONTROL);
	/* Do not take over external control or a PHY test mode. */
	if (s->fault == 0 && (control & UINT32_C(0x80000300)) != 0)
		return phy_fail(s, EBUSY);
	phy_update(s, A733_HDMI_TOP32, TOP_CONTROL, 0x17, 0x17);
	phy_update(s, A733_HDMI_TOP32, TOP_PLL, PLL_OUTPUT, 0);
	phy_update(s, A733_HDMI_TOP32, TOP_PAD, 1, 0);
	phy_update(s, A733_HDMI_TOP32, TOP_CLOCK, 0x100, 0);
	/* Preserve the C422-0 spares. Keep TX off during HPD and DDC. */
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_CONF, 0x1f, 0x16);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_MASK, 0xf3, 0xf3);
	if (s->fault == 0)
		s->hpd_prepared = true;
	return s->fault;
}

static void
phy_i2c(struct sun60i_a733_hdmi_phy *s, uint8_t address,
    uint16_t *value, bool write)
{
	uint32_t status = 0;
	unsigned i;

	phy_write(s, A733_HDMI_CORE8, CORE_PHY_I2C_STATUS, 3);
	phy_write(s, A733_HDMI_CORE8, CORE_PHY_I2C_ADDR, address);
	if (write) {
		phy_write(s, A733_HDMI_CORE8, CORE_PHY_I2C_OUT_HI, *value >> 8);
		phy_write(s, A733_HDMI_CORE8, CORE_PHY_I2C_OUT_LO, *value & 255);
	}
	phy_write(s, A733_HDMI_CORE8, CORE_PHY_I2C_OP, write ? 0x10 : 1);
	for (i = 0; i < 500 && s->fault == 0; i++) {
		phy_delay(s, 10);
		status = phy_read(s, A733_HDMI_CORE8, CORE_PHY_I2C_STATUS);
		if (s->fault != 0)
			return;
		if (!write) {
			s->trace.status = status;
			s->trace.valid |= A733_HDMI_PHY_TRACE_STATUS;
		}
		if ((status & 1) != 0) {
			phy_fail(s, EIO);
			return;
		}
		if ((status & 2) != 0)
			break;
	}
	if (s->fault != 0)
		return;
	if ((status & 2) == 0) {
		phy_fail(s, ETIMEDOUT);
		return;
	}
	phy_write(s, A733_HDMI_CORE8, CORE_PHY_I2C_STATUS, 2);
	if (!write) {
		uint32_t hi, lo;

		hi = phy_read(s, A733_HDMI_CORE8, CORE_PHY_I2C_IN_HI);
		lo = phy_read(s, A733_HDMI_CORE8, CORE_PHY_I2C_IN_LO);
		if (s->fault == 0) {
			*value = (uint16_t)((hi << 8) | lo);
			s->trace.observed = *value;
			s->trace.valid |= A733_HDMI_PHY_TRACE_OBSERVED;
		}
	}
}

static void
phy_mpll(struct sun60i_a733_hdmi_phy *s,
    const struct sun60i_a733_hdmi_plan *plan)
{
	const uint8_t addresses[] = { 0x06, 0x10, 0x11, 0x19, 0x0e, 0x09 };
	uint16_t values[] = { plan->mpll[0], plan->mpll[1], plan->mpll[2],
	    0x0007, 0x8160, 0x8188 };
	uint16_t observed;
	size_t i;

	/* GEN2 reset is active high. Do not use the older GEN1 polarity. */
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_RESET, 1, 1);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_CONF, 0x18, 0x10);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_CONF, 0x20, 0);
	phy_delay(s, 5);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_CONF, 0x20, 0x20);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_RESET, 1, 0);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_JTAG, 0x11, 0x10);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_I2C_SLAVE, 0x7f, 0x69);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_I2C_INT, 4, 0);
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_I2C_CTLINT, 0x44, 0);
	for (i = 0; i < sizeof(addresses) / sizeof(addresses[0]); i++) {
		if (s->fault == 0) {
			s->trace = (struct sun60i_a733_hdmi_phy_trace){
			    .version = 1, .valid = A733_HDMI_PHY_TRACE_REQUEST,
			    .index = i, .address = addresses[i],
			    .requested = values[i]
			};
		}
		phy_i2c(s, addresses[i], &values[i], true);
		observed = 0;
		phy_i2c(s, addresses[i], &observed, false);
		if (s->fault == 0) {
			s->trace.valid |= A733_HDMI_PHY_TRACE_COMPARED;
			if (observed != values[i])
				phy_fail(s, EIO);
		}
	}
	phy_update(s, A733_HDMI_CORE8, CORE_PHY_CONF, 0x18, 8);
	phy_wait(s, A733_HDMI_CORE8, CORE_PHY_STATUS, 1, 1, 500, 5);
}

int
sun60i_a733_hdmi_phy_prepare(struct sun60i_a733_hdmi_phy *s,
    uint32_t dcxo, uint32_t pixel)
{
	struct sun60i_a733_hdmi_plan plan;
	int error;

	if ((error = phy_ready(s)) != 0)
		return error;
	if (!s->hpd_prepared)
		return phy_fail(s, EINVAL);
	if (s->phy_prepared)
		return phy_fail(s, EALREADY);
	if ((error = sun60i_a733_hdmi_plan(dcxo, pixel, &plan)) != 0)
		return phy_fail(s, error);
	if ((phy_read(s, A733_HDMI_CORE8, CORE_PHY_STATUS) & 2) == 0)
		phy_fail(s, ENXIO);
	phy_update(s, A733_HDMI_TOP32, TOP_PLL, PLL_OUTPUT, 0);
	phy_update(s, A733_HDMI_TOP32, TOP_PLL, PLL_FIELDS, plan.pll & PLL_FIELDS);
	phy_update(s, A733_HDMI_TOP32, TOP_LDO, 0x0003f11f, 0x00035000);
	phy_update(s, A733_HDMI_TOP32, TOP_PATTERN0, UINT32_MAX, plan.pattern0);
	phy_update(s, A733_HDMI_TOP32, TOP_PATTERN1, 0xfb11ffff, 0x30000000);
	phy_update(s, A733_HDMI_TOP32, TOP_PLL, 0x80000000, 0x80000000);
	phy_update(s, A733_HDMI_TOP32, TOP_PLL, 0x40000000, 0x40000000);
	phy_update(s, A733_HDMI_TOP32, TOP_CLOCK, 0x80, 0x80);
	phy_update(s, A733_HDMI_TOP32, TOP_PLL, 0x20000000, 0x20000000);
	phy_wait(s, A733_HDMI_TOP32, TOP_LOCK, 1, 1, 1000, 20);
	phy_delay(s, 20);
	phy_update(s, A733_HDMI_TOP32, TOP_PLL, PLL_OUTPUT, PLL_OUTPUT);
	phy_mpll(s, &plan);
	if (s->fault == 0 &&
	    (phy_read(s, A733_HDMI_CORE8, CORE_PHY_STATUS) & 3) != 3)
		phy_fail(s, ENXIO);
	if (s->fault == 0) {
		s->plan = plan;
		s->phy_prepared = true;
	}
	return s->fault;
}

int
sun60i_a733_hdmi_phy_observe(struct sun60i_a733_hdmi_phy *s)
{
	uint32_t pll, status, phy_status;
	int error;

	if ((error = phy_ready(s)) != 0)
		return error;
	if (!s->phy_prepared)
		return phy_fail(s, EINVAL);
	pll = phy_read(s, A733_HDMI_TOP32, TOP_PLL);
	status = phy_read(s, A733_HDMI_TOP32, TOP_LOCK);
	phy_status = phy_read(s, A733_HDMI_CORE8, CORE_PHY_STATUS);
	if (s->fault == 0 &&
	    ((pll & (PLL_FIELDS | PLL_ENABLE | PLL_OUTPUT)) != s->plan.pll ||
	    (status & 1) == 0 || (phy_status & 3) != 3))
		phy_fail(s, EIO);
	return s->fault;
}
