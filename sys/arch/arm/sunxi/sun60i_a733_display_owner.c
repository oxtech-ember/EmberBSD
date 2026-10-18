/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD, AI-assisted. Native display arbitration and borrowed
 * ancestor checks. This code does not access or enable display hardware.
 * Register facts and license boundary: ember/boot/a733-display-ownership.md.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_display_owner.h>

int
sun60i_a733_display_running_ancestors(
    const struct sun60i_a733_display_inputs *in,
    struct sun60i_a733_display_rates *out)
{
	const uint32_t running = __BITS(31,27);
	struct sun60i_a733_display_rates rates = { 0 };
	const uint32_t *v;
	uint64_t reference, numerator, periph;
	u_int dcxo, n, selector, gate_mask, denominator;

	if (in == NULL || out == NULL)
		return EINVAL;
	if (memcmp(in->ancestor[0], in->ancestor[1],
	    sizeof(in->ancestor[0])) != 0 ||
	    in->dcxo[0][0] != in->dcxo[0][1] ||
	    in->dcxo[0][0] != in->dcxo[1][0] ||
	    in->dcxo[0][0] != in->dcxo[1][1])
		return EBUSY;
	v = in->ancestor[0];
	selector = (in->dcxo[0][0] >> 14) & 3;
	dcxo = selector == 1 ? 19200000 : selector == 2 ? 26000000 : 24000000;
	if ((v[A733_DISPLAY_ANCESTOR_REF] & running) != running ||
	    (v[A733_DISPLAY_ANCESTOR_PERIPH] & running) != running)
		return EBUSY;
	/* Same integer REF normalization as the native GPU provider. */
	reference = (uint64_t)dcxo *
	    (((v[A733_DISPLAY_ANCESTOR_REF] >> 8) & 255) + 1);
	denominator = (((v[A733_DISPLAY_ANCESTOR_REF] >> 16) & 127) + 1) *
	    (((v[A733_DISPLAY_ANCESTOR_REF] >> 1) & 1) + 1);
	if (reference != UINT64_C(24000000) * denominator)
		return EOPNOTSUPP;
	if ((v[A733_DISPLAY_ANCESTOR_PAT0] & __BIT(31)) != 0 ||
	    (v[A733_DISPLAY_ANCESTOR_PAT1] & UINT32_C(0x8f100000)) != 0)
		return EOPNOTSUPP;
	n = ((v[A733_DISPLAY_ANCESTOR_PERIPH] >> 8) & 255) + 1;
	if (n < 11)
		return EOPNOTSUPP;
	numerator = UINT64_C(24000000) * n;
	denominator = (((v[A733_DISPLAY_ANCESTOR_PERIPH] >> 20) & 7) + 1) *
	    (((v[A733_DISPLAY_ANCESTOR_PERIPH] >> 1) & 1) + 1);
	if (numerator % denominator != 0)
		return EOPNOTSUPP;
	periph = numerator / denominator;
	/* Fixed first DE0 profile: selector 3, divide by one, no PLL retune. */
	if (periph != UINT64_C(1200000000))
		return EOPNOTSUPP;
	gate_mask = __BIT(1) | __BIT(2); /* 400M and 400M_ALL. */
	selector = (v[A733_DISPLAY_ANCESTOR_AHB] >> 24) & 3;
	if (selector == 0)
		numerator = 24000000;
	else if (selector == 3) {
		numerator = periph / 2;
		gate_mask |= __BIT(9); /* Borrow the 600M branch for AHB. */
	} else
		return EOPNOTSUPP;
	denominator = (v[A733_DISPLAY_ANCESTOR_AHB] & 31) + 1;
	if (numerator % denominator != 0)
		return EOPNOTSUPP;
	/* Configured, manual and effective gates must all remain open. */
	if ((v[A733_DISPLAY_ANCESTOR_GATE] & gate_mask) != gate_mask ||
	    ((v[A733_DISPLAY_ANCESTOR_GATE] >> 16) & gate_mask) != gate_mask ||
	    ((v[A733_DISPLAY_ANCESTOR_STATUS] >> 16) & gate_mask) != gate_mask)
		return EBUSY;
	rates.reference_hz = 24000000;
	rates.periph400_hz = 400000000;
	rates.ahb_hz = numerator / denominator;
	*out = rates;
	return 0;
}

static int
sun60i_a733_display_initial(const struct sun60i_a733_display_inputs *in)
{
	struct sun60i_a733_display_clock_facts facts;
	const uint32_t *v = in->clocks.sample[0];
	int error;

	error = sun60i_a733_display_decode_clocks(&in->clocks, &facts);
	if (error != 0)
		return error;
	if (in->power.changed != 0 ||
	    memcmp(in->power.sample[0], in->power.sample[1],
	    sizeof(in->power.sample[0])) != 0 ||
	    memcmp(in->ancestor[0], in->ancestor[1],
	    sizeof(in->ancestor[0])) != 0 ||
	    in->dcxo[0][0] != in->dcxo[0][1] ||
	    in->dcxo[0][0] != in->dcxo[1][0] ||
	    in->dcxo[0][0] != in->dcxo[1][1])
		return EBUSY;
	if (facts.unknown_selectors != 0)
		return EOPNOTSUPP;
	if (facts.update_pending != 0 || facts.dynamic_mode != 0)
		return EBUSY;
	for (u_int d = 0; d < A733_DISPLAY_NDOMAINS; d++) {
		const uint32_t *p = in->power.sample[0][d];
		/* A locked static ON policy is safe to borrow without a write. */
		if ((p[0] & ~__BIT(12)) != 8 || p[1] != 0 || p[2] != 8)
			return EBUSY;
	}
	if ((v[A733_DISPLAY_DE0] & __BIT(31)) != 0 ||
	    (v[A733_DISPLAY_DE0_BUS] & (__BIT(16) | __BIT(0))) != 0 ||
	    (v[A733_DISPLAY_DE_SYS_RESET] & __BIT(16)) == 0 ||
	    (v[A733_DISPLAY_VO1_RESET] & __BIT(16)) == 0)
		return EBUSY;
	/* Borrow open DE/VO1 master paths without enabling auto gating. */
	if ((v[A733_DISPLAY_AHB] & (__BIT(5) | __BIT(4))) !=
	    (__BIT(5) | __BIT(4)) || (v[A733_DISPLAY_AHB] & __BIT(31)) != 0 ||
	    (v[A733_DISPLAY_MBUS] & __BIT(11)) == 0)
		return EBUSY;
	/* A stopped PLL_DE is valid. This reservation does not start it. */
	return 0;
}

int
sun60i_a733_display_writer_enter(struct sun60i_a733_display_owner *state,
    enum sun60i_a733_display_resource resource)
{
	if (state == NULL || (u_int)resource > A733_DISPLAY_RES_OTHER)
		return EINVAL;
	if (state->checking ||
	    (state->owner != NULL && resource != A733_DISPLAY_RES_OTHER))
		return EBUSY;
	if (state->writers == UINT_MAX)
		return EOVERFLOW;
	state->writers++;
	return 0;
}

int
sun60i_a733_display_writer_exit(struct sun60i_a733_display_owner *state)
{
	if (state == NULL || state->writers == 0)
		return EINVAL;
	state->writers--;
	return 0;
}

int
sun60i_a733_display_reserve(struct sun60i_a733_display_owner *state,
    const void *owner, sun60i_a733_display_inputs_fn read, void *arg)
{
	struct sun60i_a733_display_inputs in = { 0 };
	int error;

	if (state == NULL || owner == NULL || read == NULL)
		return EINVAL;
	if (state->owner != NULL || state->writers != 0 || state->checking)
		return EBUSY;
	state->checking = true;
	error = read(arg, &in);
	if (error == 0)
		error = sun60i_a733_display_initial(&in);
	state->checking = false;
	if (error != 0)
		return error;
	state->before = in;
	state->owner = owner;
	return 0;
}

int
sun60i_a733_display_release(struct sun60i_a733_display_owner *state,
    const void *owner)
{
	if (state == NULL || owner == NULL || state->owner != owner)
		return EINVAL;
	if (state->retained || state->checking || state->fault != 0)
		return EBUSY;
	state->owner = NULL;
	memset(&state->before, 0, sizeof(state->before));
	memset(&state->rates, 0, sizeof(state->rates));
	return 0;
}

int
sun60i_a733_display_begin_de0(struct sun60i_a733_display_owner *state,
    const void *owner, sun60i_a733_display_inputs_fn read, void *arg)
{
	struct sun60i_a733_display_inputs in = { 0 };
	struct sun60i_a733_display_rates rates;
	int error;

	if (state == NULL || owner == NULL || state->owner != owner || read == NULL)
		return EINVAL;
	if (state->retained || state->checking || state->writers != 0 ||
	    state->fault != 0)
		return EBUSY;
	state->checking = true;
	error = read(arg, &in);
	if (error == 0)
		error = sun60i_a733_display_initial(&in);
	if (error == 0 &&
	    (memcmp(state->before.clocks.sample, in.clocks.sample,
	    sizeof(in.clocks.sample)) != 0 ||
	    memcmp(state->before.power.sample, in.power.sample,
	    sizeof(in.power.sample)) != 0 ||
	    memcmp(state->before.ancestor, in.ancestor, sizeof(in.ancestor)) != 0 ||
	    memcmp(state->before.dcxo, in.dcxo, sizeof(in.dcxo)) != 0))
		error = EBUSY;
	if (error == 0)
		error = sun60i_a733_display_running_ancestors(&in, &rates);
	state->checking = false;
	if (error != 0)
		return error;
	/* The native caller must set this boundary before any write attempt. */
	state->retained = true;
	state->rates = rates;
	return 0;
}

int
sun60i_a733_display_fault(struct sun60i_a733_display_owner *state,
    const void *owner, int error)
{
	if (state == NULL || owner == NULL || state->owner != owner ||
	    !state->retained || error <= 0)
		return EINVAL;
	if (state->fault == 0)
		state->fault = error;
	return 0;
}

int
sun60i_a733_display_owner_status(const struct sun60i_a733_display_owner *state,
    struct sun60i_a733_display_owner_status *out)
{
	struct sun60i_a733_display_owner_status status = { 0 };

	if (state == NULL || out == NULL)
		return EINVAL;
	status.reserved = state->owner != NULL;
	status.retained = state->retained;
	status.writers = state->writers;
	status.fault = state->fault;
	status.rates = state->rates;
	*out = status;
	return 0;
}
