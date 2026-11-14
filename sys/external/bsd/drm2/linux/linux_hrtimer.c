/*	$NetBSD: linux_hrtimer.c,v 1.3 2021/12/19 11:55:47 riastradh Exp $	*/

/*-
 * Copyright (c) 2021 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: linux_hrtimer.c,v 1.3 2021/12/19 11:55:47 riastradh Exp $");

#include <sys/types.h>
#include <sys/callout.h>
#include <sys/systm.h>
#include <sys/time.h>

#include <linux/hrtimer.h>
#include <linux/ktime.h>

static void hrtimer_fire(void *);

void
hrtimer_init(struct hrtimer *hrt, clockid_t clkid, enum hrtimer_mode mode)
{

	KASSERTMSG(clkid == CLOCK_MONOTONIC, "clkid %d", clkid);

	callout_init(&hrt->hrt_ch, CALLOUT_MPSAFE);
	callout_setfunc(&hrt->hrt_ch, hrtimer_fire, hrt);
	hrt->hrt_mode = mode;
}

/*
 * The time of the timer is kept as hrt_deadline, an absolute time of the
 * monotonic clock, whatever the mode it was started with (hrt_expires keeps
 * the value that was given).  hrtimer_forward moves it by whole periods, so
 * that a function that calls hrtimer_forward_now and returns HRTIMER_RESTART
 * runs again one period after the previous expiry, which is how the 6.18
 * vblank timer works.  The callout has the resolution of the clock tick: the
 * delay is rounded up to a tick.
 */
static int64_t
_hrtimer_now_ns(void)
{
	struct timespec ts;

	nanouptime(&ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static void
_hrtimer_schedule(struct hrtimer *hrt)
{
	int64_t delta = hrt->hrt_deadline - _hrtimer_now_ns();
	int64_t nticks;

	if (delta <= 0)
		nticks = 1;
	else {
		nticks = (delta * hz + 999999999) / 1000000000;
		if (nticks > INT_MAX / 2)
			nticks = INT_MAX / 2;
		if (nticks < 1)
			nticks = 1;
	}
	callout_schedule(&hrt->hrt_ch, (int)nticks);
}

static void
hrtimer_fire(void *cookie)
{
	struct hrtimer *hrt = cookie;

	/*
	 * The callout can fire up to a tick before the time of the timer;
	 * the function of an hrtimer never runs early (it would move the
	 * expiry by nothing and run twice in a period).
	 */
	if (_hrtimer_now_ns() < hrt->hrt_deadline) {
		_hrtimer_schedule(hrt);
		callout_ack(&hrt->hrt_ch);
		return;
	}

	switch ((*hrt->function)(hrt)) {
	case HRTIMER_RESTART:
		_hrtimer_schedule(hrt);
		break;
	case HRTIMER_NORESTART:
		break;
	}

	callout_ack(&hrt->hrt_ch);
}

void
hrtimer_set_expires(struct hrtimer *hrt, ktime_t expires)
{

	hrt->hrt_expires = expires;
	hrt->hrt_deadline = ktime_to_ns(expires);
}

void
hrtimer_add_expires_ns(struct hrtimer *hrt, uint64_t ns)
{

	hrt->hrt_expires = ktime_add_ns(hrt->hrt_expires, ns);
	hrt->hrt_deadline += (int64_t)ns;
}

void
hrtimer_start(struct hrtimer *hrt, ktime_t expires, enum hrtimer_mode mode)
{

	hrtimer_start_range_ns(hrt, expires, 0, mode);
}

void
hrtimer_start_range_ns(struct hrtimer *hrt, ktime_t expires, uint64_t range_ns,
    enum hrtimer_mode mode)
{

	hrt->hrt_expires = expires;
	(void)range_ns;
	hrt->hrt_mode = mode;
	switch (mode) {
	case HRTIMER_MODE_ABS:
		hrt->hrt_deadline = ktime_to_ns(expires);
		break;
	case HRTIMER_MODE_REL:
	case HRTIMER_MODE_REL_PINNED:
		hrt->hrt_deadline = _hrtimer_now_ns() + ktime_to_ns(expires);
		break;
	default:
		panic("invalid hrtimer mode %d", mode);
	}
	_hrtimer_schedule(hrt);
}

int
hrtimer_cancel(struct hrtimer *hrt)
{
	bool active;

	/*
	 * Halt the callout and ascertain whether the hrtimer was
	 * active when we invoked hrtimer_cancel.
	 */
	if (callout_halt(&hrt->hrt_ch, NULL)) {
		/* Callout expired, meaning it was active.  */
		active = true;
	} else {
		/*
		 * Callout had not yet expired.  It will not expire
		 * now, so callout_pending is now stable and
		 * corresponds with whether the hrtimer was active or
		 * not.
		 */
		active = callout_pending(&hrt->hrt_ch);
	}
	return active;
}

/*
 * Like hrtimer_cancel but never waits: 1 if the timer was pending and is
 * stopped, -1 if its function is running now (the caller tries again),
 * 0 if it was not active.
 */
int
hrtimer_try_to_cancel(struct hrtimer *hrt)
{
	bool pending = callout_pending(&hrt->hrt_ch);

	/*
	 * callout_stop returns true if the callout had already expired:
	 * it has run, or its function is running now.
	 */
	if (callout_stop(&hrt->hrt_ch))
		return callout_invoking(&hrt->hrt_ch) ? -1 : 0;
	return pending ? 1 : 0;
}

bool
hrtimer_active(struct hrtimer *hrt)
{

	/*
	 * If the callout has been scheduled, but has not yet fired,
	 * then it is pending.
	 *
	 * If the callout has fired, but has not yet reached
	 * callout_ack, then it is invoking.
	 */
	return callout_pending(&hrt->hrt_ch) || callout_invoking(&hrt->hrt_ch);
}

uint64_t
hrtimer_forward(struct hrtimer *hrt, ktime_t now, ktime_t period)
{
	int64_t now_ns = ktime_to_ns(now), period_ns = ktime_to_ns(period);
	uint64_t nperiods;

	KASSERT(!callout_pending(&hrt->hrt_ch));

	/* If it has not expired yet, no overruns.  */
	if (period_ns <= 0 || now_ns < hrt->hrt_deadline)
		return 0;

	/*
	 * Advance the expiry by as many periods as it takes to get past now:
	 * one more than the whole periods that have gone by.
	 */
	nperiods = (uint64_t)(now_ns - hrt->hrt_deadline) /
	    (uint64_t)period_ns + 1;
	hrt->hrt_deadline += (int64_t)nperiods * period_ns;
	hrt->hrt_expires = ns_to_ktime(hrt->hrt_deadline);
	return nperiods;
}

uint64_t
hrtimer_forward_now(struct hrtimer *hrt, ktime_t period)
{

	return hrtimer_forward(hrt, ktime_get(), period);
}
