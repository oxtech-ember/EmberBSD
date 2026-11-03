/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD; independent A133 PPU implementation from register facts. */
#ifdef _KERNEL
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#include <stddef.h>
#endif
#include <arm/sunxi/sun50i_a133_ppu_power.h>

static int
a133_ppu_valid(const struct a133_ppu_power *power)
{

	if (power == NULL || power->io.read == NULL || power->io.write == NULL ||
	    power->io.delay == NULL || power->revision > 7)
		return EINVAL;
	return power->uncertain ? EIO : 0;
}

static int
a133_ppu_state(struct a133_ppu_power *power, unsigned *state)
{
	uint32_t reset, clock, isolation, value;
	int error;

	if ((error = power->io.read(power->io.cookie, A133_PPU_RSTN, &reset)) != 0 ||
	    (error = power->io.read(power->io.cookie, A133_PPU_CLOCK, &clock)) != 0 ||
	    (error = power->io.read(power->io.cookie, A133_PPU_ISOLATION,
	    &isolation)) != 0 ||
	    (error = power->io.read(power->io.cookie, A133_PPU_STATE, &value)) != 0)
		return error;
	reset &= 1;
	clock &= 1;
	isolation &= 3;
	value &= 3;
	if ((value == A133_PPU_OFF && reset == 0 && clock == 0 && isolation == 3) ||
	    (value == A133_PPU_3D && reset == 1 && clock == 1 && isolation == 0) ||
	    (value == A133_PPU_HOST && reset == 1 && clock == 1 && isolation == 2)) {
		*state = value;
		return 0;
	}
	return EIO;
}

static int
a133_ppu_update(struct a133_ppu_power *power, unsigned reg,
    uint32_t mask, uint32_t value)
{
	uint32_t old;
	int error;

	error = power->io.read(power->io.cookie, reg, &old);
	if (error != 0)
		return error;
	return power->io.write(power->io.cookie, reg, (old & ~mask) | value);
}

static int
a133_ppu_domain_b_on(struct a133_ppu_power *power)
{
	static const unsigned switches[] = { 23, 18, 16, 19, 20, 21, 22, 17 };

	for (unsigned i = 0; i < sizeof(switches) / sizeof(switches[0]); i++) {
		const int error = power->io.write(power->io.cookie, A133_PPU_PSWON,
		    UINT32_C(1) << switches[i]);
		if (error != 0)
			return error;
		power->io.delay(power->io.cookie, i < 2 ? 10 : 5);
	}
	return a133_ppu_update(power, A133_PPU_ISOLATION, 2, 0);
}

static int
a133_ppu_domain_b_off(struct a133_ppu_power *power)
{
	int error;

	error = a133_ppu_update(power, A133_PPU_ISOLATION, 2, 2);
	if (error != 0)
		return error;
	return power->io.write(power->io.cookie, A133_PPU_PSWOFF, 0xff0000);
}

static int
a133_ppu_full_on(struct a133_ppu_power *power)
{
	int error;

	for (unsigned i = 0; i < 8; i++) {
		error = power->io.write(power->io.cookie, A133_PPU_PSWON, 1U << i);
		if (error != 0)
			return error;
		power->io.delay(power->io.cookie, 1);
	}
	error = power->io.write(power->io.cookie, A133_PPU_CLOCK, 1);
	if (error != 0)
		return error;
	power->io.delay(power->io.cookie, 1);
	if ((error = a133_ppu_update(power, A133_PPU_ISOLATION, 1, 0)) != 0 ||
	    (error = a133_ppu_domain_b_on(power)) != 0)
		return error;
	power->io.delay(power->io.cookie, 1);
	error = power->io.write(power->io.cookie, A133_PPU_RSTN, 1);
	if (error != 0)
		return error;
	power->io.delay(power->io.cookie, 10);
	return power->io.write(power->io.cookie, A133_PPU_STATE, A133_PPU_3D);
}

static int
a133_ppu_full_off(struct a133_ppu_power *power)
{
	int error;

	if ((error = a133_ppu_update(power, A133_PPU_ISOLATION, 2, 2)) != 0 ||
	    (error = a133_ppu_update(power, A133_PPU_ISOLATION, 1, 1)) != 0 ||
	    (error = power->io.write(power->io.cookie, A133_PPU_CLOCK, 0)) != 0)
		return error;
	power->io.delay(power->io.cookie, 1);
	if ((error = power->io.write(power->io.cookie, A133_PPU_RSTN, 0)) != 0 ||
	    (error = power->io.write(power->io.cookie, A133_PPU_PSWOFF,
	    0xff0000)) != 0 ||
	    (error = power->io.write(power->io.cookie, A133_PPU_PSWOFF, 0xff)) != 0)
		return error;
	return power->io.write(power->io.cookie, A133_PPU_STATE, A133_PPU_OFF);
}

int
a133_ppu_power_get(struct a133_ppu_power *power, bool *enabled)
{
	uint32_t request;
	unsigned state;
	int error;

	if (enabled == NULL)
		return EINVAL;
	if ((error = a133_ppu_valid(power)) != 0 ||
	    (error = a133_ppu_state(power, &state)) != 0 ||
	    (error = power->io.read(power->io.cookie, A133_PPU_REQUEST,
	    &request)) != 0)
		return error;
	if ((request & 1) != 0)
		return EBUSY;
	*enabled = state == A133_PPU_3D;
	return 0;
}

int
a133_ppu_power_prepare(struct a133_ppu_power *power, bool enable, bool *change)
{
	uint32_t request;
	unsigned state;
	int error;

	if (change == NULL)
		return EINVAL;
	if ((error = a133_ppu_valid(power)) != 0 ||
	    (error = a133_ppu_state(power, &state)) != 0 ||
	    (error = power->io.read(power->io.cookie, A133_PPU_REQUEST,
	    &request)) != 0)
		return error;
	if ((request & 1) != 0)
		return EBUSY;
	if ((enable && state == A133_PPU_3D) || (!enable && state == A133_PPU_OFF)) {
		*change = false;
		return 0;
	}
	if (power->revision != 6 || (enable && state != A133_PPU_OFF) ||
	    (!enable && !power->owned))
		return EOPNOTSUPP;
	*change = true;
	return 0;
}

int
a133_ppu_power_set(struct a133_ppu_power *power, bool enable)
{
	uint32_t control;
	unsigned state;
	bool change;
	int error;

	error = a133_ppu_power_prepare(power, enable, &change);
	if (error != 0 || !change)
		return error;
	if ((error = power->io.read(power->io.cookie, A133_PPU_CONTROL,
	    &control)) != 0)
		return error;
	if ((control & 3) != 1)
		return EOPNOTSUPP;
	/* The caller has established the interrupt before any unmask. */
	error = power->io.write(power->io.cookie, A133_PPU_IRQ_MASK, 0);
	if (error == 0)
		error = enable ? a133_ppu_full_on(power) : a133_ppu_full_off(power);
	if (error == 0) {
		const unsigned expected = enable ? A133_PPU_3D : A133_PPU_OFF;
		error = a133_ppu_state(power, &state);
		if (error == 0 && state != expected)
			error = EIO;
	}
	if (error == 0)
		error = power->io.write(power->io.cookie, A133_PPU_REQUEST, 1);
	if (error == 0 && enable)
		error = power->io.write(power->io.cookie, A133_PPU_IRQ_MASK, 1);
	if (error != 0) {
		power->uncertain = true;
		return error;
	}
	power->owned = enable;
	return 0;
}

int
a133_ppu_service_request(struct a133_ppu_power *power, bool *handled)
{
	uint32_t request, control;
	unsigned state, transition;
	int error;

	if (handled == NULL)
		return EINVAL;
	if ((error = a133_ppu_valid(power)) != 0)
		return error;
	if (power->revision != 0 && power->revision != 3 &&
	    power->revision != 4 && power->revision != 5 && power->revision != 6)
		return EOPNOTSUPP;
	if ((error = power->io.read(power->io.cookie, A133_PPU_REQUEST,
	    &request)) != 0)
		return error;
	if ((request & 1) == 0) {
		*handled = false;
		return 0;
	}
	transition = (request >> 16) & 7;
	if ((error = a133_ppu_state(power, &state)) != 0 ||
	    (error = power->io.read(power->io.cookie, A133_PPU_CONTROL,
	    &control)) != 0)
		return error;
	if ((control & 3) != 1 ||
	    !((transition == 2 && state == A133_PPU_3D) ||
	      (transition == 4 && state == A133_PPU_HOST))) {
		/* Mask the source, without acknowledging an unknown transition. */
		power->uncertain = true;
		error = power->io.write(power->io.cookie, A133_PPU_IRQ_MASK, 0);
		return error != 0 ? error : EPROTO;
	}
	error = transition == 2 ? a133_ppu_domain_b_off(power) :
	    a133_ppu_domain_b_on(power);
	if (error == 0)
		error = power->io.write(power->io.cookie, A133_PPU_STATE,
		    transition == 2 ? A133_PPU_HOST : A133_PPU_3D);
	if (error == 0) {
		error = a133_ppu_state(power, &state);
		if (error == 0 && state != (transition == 2 ? A133_PPU_HOST : A133_PPU_3D))
			error = EIO;
	}
	if (error == 0)
		error = power->io.write(power->io.cookie, A133_PPU_REQUEST, 1);
	if (error == 0)
		error = a133_ppu_update(power, A133_PPU_CONTROL, 0x10, 0x10);
	if (error != 0) {
		power->uncertain = true;
		return error;
	}
	*handled = true;
	return 0;
}
