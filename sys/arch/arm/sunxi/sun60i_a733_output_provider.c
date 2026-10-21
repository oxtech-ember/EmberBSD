/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; native A733 output dispatch and lifetime. */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/mutex.h>
#include <sys/once.h>
#include <sys/systm.h>
#include <arm/sunxi/sun60i_a733_output.h>
#include <arm/sunxi/sun60i_a733_hdmi_phy.h>

static ONCE_DECL(output_once);
static kmutex_t output_lock;
static int output_phandle;
static const struct sun60i_a733_output_ops *output_ops;
static void *output_arg;
static const void *output_owner;
static struct sun60i_a733_output_mode output_mode;
static bool output_mode_valid, output_prepared, output_started;
static int output_error;

static int
output_init(void)
{
	mutex_init(&output_lock, MUTEX_DEFAULT, IPL_NONE);
	return 0;
}

static int
output_lookup(int phandle, const void *owner)
{
	KASSERT(mutex_owned(&output_lock));
	if (output_ops == NULL || phandle != output_phandle)
		return ENXIO;
	if (owner == NULL)
		return EINVAL;
	if (output_owner != NULL && owner != output_owner)
		return EBUSY;
	return output_error;
}

static void
output_fail(int error)
{
	KASSERT(mutex_owned(&output_lock));
	if (output_error == 0) {
		output_error = error;
		output_ops->fault(output_arg, output_owner, error);
	}
}

int
sun60i_a733_output_register(int phandle,
    const struct sun60i_a733_output_ops *ops, void *arg)
{
	int error;

	if (phandle <= 0 || ops == NULL || ops->get_mode == NULL ||
	    ops->prepare == NULL || ops->start == NULL ||
	    ops->observed_vblank == NULL || ops->fault == NULL || arg == NULL)
		return EINVAL;
	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	error = output_ops != NULL ? EBUSY : 0;
	if (error == 0) {
		output_phandle = phandle;
		output_ops = ops;
		output_arg = arg;
	}
	mutex_exit(&output_lock);
	return error;
}

int
sun60i_a733_output_get_mode(int phandle, const void *owner,
    struct sun60i_a733_output_mode *out)
{
	struct sun60i_a733_output_mode mode = { 0 };
	struct sun60i_a733_tcon_timing timing;
	int error;

	if (out == NULL)
		return EINVAL;
	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	error = output_lookup(phandle, owner);
	if (error == 0 && !output_mode_valid) {
		/* Retain the token before a callback can attempt a DDC write. */
		output_owner = owner;
		error = output_ops->get_mode(output_arg, owner, &mode);
		if (error == 0)
			error = sun60i_a733_tcon_timing_encode(&mode, &timing);
		if (error != 0)
			output_fail(error);
		else {
			output_mode = mode;
			output_mode_valid = true;
		}
	}
	if (error == 0)
		*out = output_mode;
	mutex_exit(&output_lock);
	return error;
}

static bool
output_same_mode(const struct sun60i_a733_output_mode *a,
    const struct sun60i_a733_output_mode *b)
{
	return a->pixel_clock_hz == b->pixel_clock_hz &&
	    a->hdisplay == b->hdisplay && a->hsync_start == b->hsync_start &&
	    a->hsync_end == b->hsync_end && a->htotal == b->htotal &&
	    a->vdisplay == b->vdisplay && a->vsync_start == b->vsync_start &&
	    a->vsync_end == b->vsync_end && a->vtotal == b->vtotal &&
	    a->hsync_positive == b->hsync_positive &&
	    a->vsync_positive == b->vsync_positive;
}

int
sun60i_a733_output_prepare(int phandle, const void *owner,
    const struct sun60i_a733_output_mode *mode)
{
	int error;

	if (mode == NULL)
		return EINVAL;
	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	error = output_lookup(phandle, owner);
	if (error == 0 && (!output_mode_valid ||
	    !output_same_mode(mode, &output_mode)))
		error = EINVAL;
	if (error == 0 && output_prepared)
		error = EALREADY;
	if (error == 0) {
		error = output_ops->prepare(output_arg, owner, mode);
		if (error != 0)
			output_fail(error);
		else
			output_prepared = true;
	}
	mutex_exit(&output_lock);
	return error;
}

int
sun60i_a733_output_start(int phandle, const void *owner)
{
	int error;

	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	error = output_lookup(phandle, owner);
	if (error == 0 && !output_prepared)
		error = EINVAL;
	if (error == 0 && output_started)
		error = EALREADY;
	if (error == 0) {
		error = output_ops->start(output_arg, owner);
		if (error != 0)
			output_fail(error);
		else
			output_started = true;
	}
	mutex_exit(&output_lock);
	return error;
}

int
sun60i_a733_output_observed_vblank(int phandle, const void *owner, uint64_t *out)
{
	uint64_t sequence;
	int error;

	if (out == NULL)
		return EINVAL;
	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	error = output_lookup(phandle, owner);
	if (error == 0 && !output_started)
		error = EINVAL;
	if (error == 0) {
		error = output_ops->observed_vblank(output_arg, owner, &sequence);
		if (error != 0 && error != ENOENT)
			output_fail(error);
		if (error == 0)
			*out = sequence;
	}
	mutex_exit(&output_lock);
	return error;
}

int
sun60i_a733_output_fault(int phandle, const void *owner, int failure)
{
	int error;

	if (failure <= 0)
		return EINVAL;
	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	error = output_lookup(phandle, owner);
	if (error == 0 && output_owner == NULL)
		error = EINVAL;
	if (error == 0)
		output_fail(failure);
	mutex_exit(&output_lock);
	return error;
}

/* The diagnostic callback uses the same mutex and lifetime as the provider. */
int
sun60i_a733_output_cached_trace(int phandle,
    struct sun60i_a733_output_trace *out)
{
	struct sun60i_a733_output_trace trace = { 0 };
	int error = 0;

	CTASSERT(sizeof(trace) == 13 * sizeof(uint32_t));
	if (out == NULL)
		return EINVAL;
	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	/* output_error must not hide the cache that explains that error. */
	if (output_ops == NULL || phandle != output_phandle)
		error = ENXIO;
	else if (output_ops->cached_trace == NULL)
		error = EOPNOTSUPP;
	else {
		output_ops->cached_trace(output_arg, &trace);
		*out = trace;
	}
	mutex_exit(&output_lock);
	return error;
}

/* This cache remains readable after a sticky output failure. */
int
sun60i_a733_output_cached_phy_trace(int phandle,
    struct sun60i_a733_hdmi_phy_trace *out)
{
	struct sun60i_a733_hdmi_phy_trace trace = { 0 };
	int error = 0;

	CTASSERT(sizeof(trace) == 7 * sizeof(uint32_t));
	if (out == NULL)
		return EINVAL;
	RUN_ONCE(&output_once, output_init);
	mutex_enter(&output_lock);
	if (output_ops == NULL || phandle != output_phandle)
		error = ENXIO;
	else if (output_ops->cached_phy_trace == NULL)
		error = EOPNOTSUPP;
	else {
		output_ops->cached_phy_trace(output_arg, &trace);
		*out = trace;
	}
	mutex_exit(&output_lock);
	return error;
}
