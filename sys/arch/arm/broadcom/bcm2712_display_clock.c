/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Shared BSD clock lease mechanism. Fixed profile policy facts: Raspberry Pi Linux
 * a1a601d4857978d810175365550c4536701df4ed, clk-raspberrypi.c and CCF.
 * No Linux implementation code is used. DISP is an explicit opt-in profile.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <arm/broadcom/bcm2712_display.h>
#include <arm/broadcom/bcm2712_display_clockvar.h>
#include <arch/evbarm/rpi/vcprop.h>

static int
bcm2712_clock_property(struct bcm2712_clock *sc, uint32_t tag, uint32_t value,
    uint32_t *out)
{
	uint32_t packet[9] __aligned(16);
	size_t words = tag == VCPROPTAG_SET_CLOCKRATE ? 9 : 8;
	uint32_t size = (uint32_t)(words * sizeof(*packet));
	uint32_t id;
	uint32_t capacity = words == 9 ? 12 : 8;
	int error;

	switch (sc->profile) {
	case BCM2712_CLOCK_HSM:
		id = VCPROP_CLK_M2MC;
		break;
	case BCM2712_CLOCK_BVB:
		id = VCPROP_CLK_PIXEL_BVB;
		break;
	case BCM2712_CLOCK_DISP:
		id = VCPROP_CLK_DISP;
		break;
	case BCM2712_CLOCK_CORE:
		id = VCPROP_CLK_CORE;
		break;
	default:
		return EINVAL;
	}
	memset(packet, 0, sizeof(packet));
	packet[0] = htole32(size);
	packet[2] = htole32(tag);
	packet[3] = htole32(capacity);
	packet[5] = htole32(id);
	packet[6] = htole32(value);
	if (tag == VCPROPTAG_SET_CLOCKRATE)
		packet[7] = htole32(1); /* Do not request turbo rates. */
	error = sc->exchange(sc->arg, packet, size);
	if (error != 0)
		return error;
	if (le32toh(packet[0]) != size ||
	    le32toh(packet[1]) != VCPROP_REQ_SUCCESS ||
	    le32toh(packet[2]) != tag || le32toh(packet[3]) != capacity ||
	    le32toh(packet[4]) != (VCPROPTAG_RESPONSE | 8) ||
	    le32toh(packet[5]) != id || packet[words - 1] != 0)
		return EIO;
	value = le32toh(packet[6]);
	if (tag == VCPROPTAG_GET_CLOCKSTATE || tag == VCPROPTAG_SET_CLOCKSTATE) {
		if ((value & ~3U) != 0)
			return EIO;
		if ((value & 2U) != 0) /* Bit 1 means the clock does not exist. */
			return ENODEV;
	}
	*out = value;
	return 0;
}

static int
bcm2712_clock_snapshot(struct bcm2712_clock *sc, uint32_t *state, uint32_t *rate,
    uint32_t *maximum)
{
	int error;

	error = bcm2712_clock_property(sc, VCPROPTAG_GET_CLOCKSTATE, 0, state);
	if (error != 0)
		return error;
	error = bcm2712_clock_property(sc, VCPROPTAG_GET_CLOCKRATE, 0, rate);
	if (error != 0)
		return error;
	return bcm2712_clock_property(sc, VCPROPTAG_GET_MAX_CLOCKRATE, 0, maximum);
}

int
bcm2712_clock_init(struct bcm2712_clock *out,
    enum bcm2712_clock_profile profile, bcm2712_clock_exchange_fn exchange,
    void *arg)
{
	struct bcm2712_clock sc;
	uint32_t maximum;
	int error;

	if (out == NULL || exchange == NULL ||
	    (profile != BCM2712_CLOCK_HSM && profile != BCM2712_CLOCK_BVB &&
	    profile != BCM2712_CLOCK_DISP && profile != BCM2712_CLOCK_CORE))
		return EINVAL;
	memset(&sc, 0, sizeof(sc));
	sc.profile = profile;
	sc.exchange = exchange;
	sc.arg = arg;
	error = bcm2712_clock_snapshot(&sc, &sc.status.state, &sc.status.rate,
	    &maximum);
	if (error != 0)
		return error;
	if (profile == BCM2712_CLOCK_HSM) {
		sc.status.floor = MAX(BCM2712_DISPLAY_HSM_MIN_RATE,
		    sc.status.rate);
	} else {
		error = bcm2712_clock_property(&sc, VCPROPTAG_GET_MIN_CLOCKRATE,
		    0, &sc.status.floor);
		if (error != 0)
			return error;
		/* A zero firmware minimum is an unsupported unleased floor. */
		if (sc.status.floor == 0)
			return ERANGE;
	}
	/* CORE also drives shared bus consumers. Keep its inherited baseline. */
	if (profile == BCM2712_CLOCK_CORE) {
		if (sc.status.rate == 0)
			return ERANGE;
		sc.status.floor = MAX(sc.status.floor, sc.status.rate);
	}
	if (maximum < sc.status.floor)
		return ERANGE;
	sc.initial_state = sc.status.state;
	sc.initialized = true;
	*out = sc;
	return 0;
}

static int
bcm2712_clock_ready(struct bcm2712_clock *sc)
{
	if (sc == NULL || !sc->initialized)
		return ENXIO;
	return sc->status.fault;
}

/* A failed SET or its readback can have changed firmware state. Never retry. */
static int
bcm2712_clock_apply(struct bcm2712_clock *sc, uint32_t wanted)
{
	uint32_t state, rate, maximum, reply;
	bool attempted = false;
	int error;

	error = bcm2712_clock_snapshot(sc, &state, &rate, &maximum);
	if (error != 0)
		return error;
	if (wanted > maximum)
		return ERANGE;
	if (rate < wanted ||
	    ((sc->profile != BCM2712_CLOCK_HSM || sc->rate_changed) &&
	    rate != wanted)) {
		attempted = true;
		sc->status.mutation_attempted = true;
		error = bcm2712_clock_property(sc, VCPROPTAG_SET_CLOCKRATE,
		    wanted, &reply);
		if (error != 0)
			goto fail;
		if (reply < wanted || reply > maximum) {
			error = ERANGE;
			goto fail;
		}
		sc->rate_changed = true;
		error = bcm2712_clock_property(sc, VCPROPTAG_GET_CLOCKRATE, 0, &rate);
		if (error != 0)
			goto fail;
		if (rate < wanted || rate > maximum) {
			error = EIO;
			goto fail;
		}
	}
	if (state == 0) {
		attempted = true;
		sc->status.mutation_attempted = true;
		error = bcm2712_clock_property(sc, VCPROPTAG_SET_CLOCKSTATE,
		    1, &reply);
		if (error != 0)
			goto fail;
		if (reply != 1) {
			error = EIO;
			goto fail;
		}
		sc->state_changed = true;
	}
	error = bcm2712_clock_property(sc, VCPROPTAG_GET_CLOCKSTATE, 0, &state);
	if (error != 0)
		goto fail;
	error = bcm2712_clock_property(sc, VCPROPTAG_GET_CLOCKRATE, 0, &rate);
	if (error != 0)
		goto fail;
	if (state != 1 || rate < wanted || rate > maximum) {
		error = EIO;
		goto fail;
	}
	sc->status.state = state;
	sc->status.rate = rate;
	return 0;
fail:
	if (attempted)
		sc->status.fault = error;
	return error;
}

static int
bcm2712_clock_change(struct bcm2712_clock *sc, unsigned int port,
    uint32_t minimum,
    bool active)
{
	uint32_t wanted = sc->status.floor;
	unsigned int i;
	int error;

	for (i = 0; i < BCM2712_CLOCK_PORTS; i++) {
		if (i == port) {
			if (active)
				wanted = MAX(wanted, minimum);
		} else if ((sc->status.leases & (1U << i)) != 0) {
			wanted = MAX(wanted, sc->status.minimum[i]);
		}
	}
	error = bcm2712_clock_apply(sc, wanted);
	if (error != 0)
		return error;
	sc->status.minimum[port] = active ? minimum : 0;
	if (active)
		sc->status.leases |= 1U << port;
	else
		sc->status.leases &= ~(1U << port);
	return 0;
}

int
bcm2712_clock_acquire(struct bcm2712_clock *sc, unsigned int port,
    uint32_t minimum)
{
	int error = bcm2712_clock_ready(sc);

	if (error != 0)
		return error;
	if (port >= BCM2712_CLOCK_PORTS || minimum == 0)
		return EINVAL;
	if ((sc->status.leases & (1U << port)) != 0)
		return EBUSY;
	return bcm2712_clock_change(sc, port, minimum, true);
}

int
bcm2712_clock_update(struct bcm2712_clock *sc, unsigned int port,
    uint32_t minimum)
{
	int error = bcm2712_clock_ready(sc);

	if (error != 0)
		return error;
	if (port >= BCM2712_CLOCK_PORTS || minimum == 0)
		return EINVAL;
	if ((sc->status.leases & (1U << port)) == 0)
		return ENOENT;
	return bcm2712_clock_change(sc, port, minimum, true);
}

int
bcm2712_clock_release(struct bcm2712_clock *sc, unsigned int port)
{
	int error = bcm2712_clock_ready(sc);

	if (error != 0)
		return error;
	if (port >= BCM2712_CLOCK_PORTS)
		return EINVAL;
	if ((sc->status.leases & (1U << port)) == 0)
		return ENOENT;
	return bcm2712_clock_change(sc, port, 0, false);
}

int
bcm2712_clock_fini(struct bcm2712_clock *sc)
{
	uint32_t state;
	int error = bcm2712_clock_ready(sc);

	if (error != 0)
		return error;
	if (sc->status.leases != 0)
		return EBUSY;
	if (sc->profile != BCM2712_CLOCK_BVB &&
	    sc->profile != BCM2712_CLOCK_CORE &&
	    sc->initial_state == 0 && sc->state_changed) {
		sc->status.mutation_attempted = true;
		error = bcm2712_clock_property(sc, VCPROPTAG_SET_CLOCKSTATE, 0,
		    &state);
		if (error == 0 && state != 0)
			error = EIO;
		if (error == 0)
			error = bcm2712_clock_property(sc, VCPROPTAG_GET_CLOCKSTATE,
			    0, &state);
		if (error == 0 && state != 0)
			error = EIO;
		if (error != 0) {
			sc->status.fault = error;
			return error;
		}
		sc->status.state = state;
	}
	/*
	 * BVB and CORE stay enabled after activation, even if initially off.
	 * HSM and DISP keep their safe floor instead of an initial zero rate.
	 */
	sc->initialized = false;
	return 0;
}

int
bcm2712_clock_status(const struct bcm2712_clock *sc,
    struct bcm2712_clock_status *out)
{
	if (out == NULL)
		return EINVAL;
	if (sc == NULL || !sc->initialized)
		return ENXIO;
	*out = sc->status;
	return 0;
}
