/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of the pwm calls in dist618/drm/i915/display/intel_backlight.c
 * and intel_display_types.h; the GPL original was not read.
 *
 * There is no PWM controller framework here: pwm_get() finds no device (an
 * error pointer), which the backlight code takes as "no external PWM" and
 * falls back to the panel's own.  The other calls act on a device that
 * pwm_get() never gives, so they refuse loudly.
 */
#ifndef _DRM618_LINUX_PWM_H_
#define _DRM618_LINUX_PWM_H_

#include <sys/types.h>
#include <sys/systm.h>

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/math64.h>

struct device;
struct pwm_device;

enum pwm_polarity {
	PWM_POLARITY_NORMAL,
	PWM_POLARITY_INVERSED,
};

struct pwm_state {
	u64			period;		/* ns */
	u64			duty_cycle;	/* ns */
	enum pwm_polarity	polarity;
	bool			enabled;
	bool			usage_power;
};

static inline struct pwm_device *
pwm_get(struct device *dev __unused, const char *con_id __unused)
{

	return ERR_PTR(-ENODEV);
}

static inline void
pwm_put(struct pwm_device *pwm)
{

	KASSERTMSG(pwm == NULL, "no pwm device can exist (%p)", pwm);
}

static inline void
pwm_get_state(const struct pwm_device *pwm, struct pwm_state *state __unused)
{

	panic("pwm_get_state(%p): no pwm device can exist", pwm);
}

static inline int
pwm_apply_might_sleep(struct pwm_device *pwm, const struct pwm_state *state __unused)
{

	panic("pwm_apply_might_sleep(%p): no pwm device can exist", pwm);
}

static inline bool
pwm_is_enabled(const struct pwm_device *pwm)
{

	panic("pwm_is_enabled(%p): no pwm device can exist", pwm);
}

/* duty cycle as DUTY out of SCALE of the period; the state alone, no device */
static inline void
pwm_set_relative_duty_cycle(struct pwm_state *state, unsigned int duty_cycle,
    unsigned int scale)
{

	if (scale == 0)
		return;
	state->duty_cycle = div_u64((u64)duty_cycle * state->period, scale);
}

static inline unsigned int
pwm_get_relative_duty_cycle(const struct pwm_state *state, unsigned int scale)
{

	if (state->period == 0)
		return 0;
	return (unsigned int)div_u64(state->duty_cycle * scale, state->period);
}

#endif
