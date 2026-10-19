/* Origin: EmberBSD serialized cold framebuffer fallback issuer. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifdef _KERNEL_OPT
#include "opt_wsdisplay_bootfb.h"
#endif
#include <sys/param.h>
#include <sys/cpu.h>
#include <sys/errno.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <dev/wscons/wsdisplay_bootfbvar.h>

enum wsdisplay_bootfb_phase {
	BOOTFB_NONE,
	BOOTFB_REGISTERING,
	BOOTFB_DEFERRED,
	BOOTFB_CLAIMED,
	BOOTFB_FALLBACK_STARTING,
	BOOTFB_FALLBACK_DONE,
	BOOTFB_RETAINED,
	BOOTFB_CLOSING,
	BOOTFB_CLOSED
};

/* One record serves both legacy reservations and the native opaque lease. */
static struct {
	enum wsdisplay_bootfb_phase phase;
	device_t device;
	device_t legacy_owner;
	int (*fallback)(device_t);
	const struct wsscreen_descr *type;
	void *cookie;
	bool native;
	bool committed;
	bool pending;
} wsdisplay_bootfb;

#ifdef WSDISPLAY_BOOTFB_DEFER
struct wsdisplay_bootfb_lease {
	bool issued;
#ifdef WSDISPLAY_BOOTFB_HANDOFF
	struct wsdisplay_bootfb_close_status close;
#endif
};
static struct wsdisplay_bootfb_lease wsdisplay_bootfb_lease;
#endif

static bool
wsdisplay_bootfb_binding(void)
{
#ifdef WSDISPLAY_BOOTFB_DEFER
	if (wsdisplay_bootfb.native)
		return wsdisplay_early_binding_matches(wsdisplay_bootfb.type,
		    wsdisplay_bootfb.cookie);
#endif
	return !wsdisplay_bootfb.native;
}

/* The real finalizer may run inside config_finalize_register itself. */
static int
wsdisplay_bootfb_finalize(device_t dev)
{
	KASSERT(KERNEL_LOCKED_P());
	if (dev != wsdisplay_bootfb.device)
		return 0;
	if (wsdisplay_bootfb.phase == BOOTFB_CLAIMED) {
		wsdisplay_bootfb.pending = true;
		return 0;
	}
	if (wsdisplay_bootfb.phase != BOOTFB_REGISTERING &&
	    wsdisplay_bootfb.phase != BOOTFB_DEFERRED)
		return 0;
	if (!wsdisplay_bootfb_binding()) {
		wsdisplay_bootfb.phase = BOOTFB_RETAINED;
		return 0;
	}
	wsdisplay_bootfb.phase = BOOTFB_FALLBACK_STARTING;
	wsdisplay_bootfb.legacy_owner = NULL;
	if (!wsdisplay_bootfb.committed)
		(void)wsdisplay_bootfb.fallback(dev);
	wsdisplay_bootfb.phase = BOOTFB_FALLBACK_DONE;
	return 0;
}

static int
wsdisplay_bootfb_register(device_t dev, int (*fallback)(device_t))
{
	int error;

	KASSERT(KERNEL_LOCKED_P());
	wsdisplay_bootfb.device = dev;
	wsdisplay_bootfb.fallback = fallback;
	wsdisplay_bootfb.phase = BOOTFB_REGISTERING;
	error = config_finalize_register(dev, wsdisplay_bootfb_finalize);
	if (error != 0) {
		/* No hook was installed. Close the window before the direct fallback. */
		wsdisplay_bootfb.phase = BOOTFB_FALLBACK_STARTING;
		(void)fallback(dev);
		wsdisplay_bootfb.phase = BOOTFB_FALLBACK_DONE;
	} else if (wsdisplay_bootfb.phase == BOOTFB_REGISTERING) {
		wsdisplay_bootfb.phase = BOOTFB_DEFERRED;
	}
	/* Even an immediate/failed registration already owns the one fallback. */
	return 0;
}

int
wsdisplay_bootfb_register_legacy(device_t dev, int (*fallback)(device_t))
{
	KASSERT(KERNEL_LOCKED_P());
	if (dev == NULL || fallback == NULL)
		return EINVAL;
	if (wsdisplay_bootfb.phase != BOOTFB_NONE)
		return EBUSY;
	return wsdisplay_bootfb_register(dev, fallback);
}

int
wsdisplay_bootfb_fallback(device_t dev, int (*fallback)(device_t))
{
	KASSERT(KERNEL_LOCKED_P());
	if (dev == NULL || fallback == NULL)
		return EINVAL;
	if (wsdisplay_bootfb.phase != BOOTFB_NONE)
		return EBUSY;
	wsdisplay_bootfb.device = dev;
	wsdisplay_bootfb.fallback = fallback;
	wsdisplay_bootfb.phase = BOOTFB_FALLBACK_STARTING;
	(void)fallback(dev);
	wsdisplay_bootfb.phase = BOOTFB_FALLBACK_DONE;
	return 0;
}

bool
wsdisplay_bootfb_reserve(device_t owner)
{
	KASSERT(KERNEL_LOCKED_P());
	if (owner == NULL || wsdisplay_bootfb.phase != BOOTFB_DEFERRED ||
	    wsdisplay_bootfb.legacy_owner != NULL || wsdisplay_bootfb.committed)
		return false;
	wsdisplay_bootfb.legacy_owner = owner;
	return true;
}

device_t
wsdisplay_bootfb_legacy_device(device_t owner)
{
	KASSERT(KERNEL_LOCKED_P());
	if (owner == NULL || wsdisplay_bootfb.phase != BOOTFB_DEFERRED ||
	    wsdisplay_bootfb.legacy_owner != owner || wsdisplay_bootfb.committed)
		return NULL;
	return wsdisplay_bootfb.device;
}

bool
wsdisplay_bootfb_release(device_t owner)
{
	if (wsdisplay_bootfb_legacy_device(owner) == NULL)
		return false;
	wsdisplay_bootfb.legacy_owner = NULL;
	return true;
}

void
wsdisplay_bootfb_commit(device_t owner)
{
	if (wsdisplay_bootfb_legacy_device(owner) == NULL) {
		KASSERTMSG(false, "invalid firmware console commit");
		return;
	}
	wsdisplay_bootfb.committed = true;
}

#ifdef WSDISPLAY_BOOTFB_DEFER
static int
wsdisplay_bootfb_context(void)
{
	if (panicstr != NULL || cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	return 0;
}

int
wsdisplay_bootfb_defer(device_t dev, const struct wsscreen_descr *type,
    void *cookie, int (*fallback)(device_t))
{
	int error;

	KASSERT(KERNEL_LOCKED_P());
	if (dev == NULL || type == NULL || cookie == NULL || fallback == NULL)
		return EINVAL;
	error = wsdisplay_bootfb_context();
	if (error != 0)
		return error;
	if (!cold || wsdisplay_bootfb.phase != BOOTFB_NONE)
		return EBUSY;
	if (!wsdisplay_early_binding_matches(type, cookie))
		return ENXIO;
	wsdisplay_bootfb.native = true;
	wsdisplay_bootfb.type = type;
	wsdisplay_bootfb.cookie = cookie;
	return wsdisplay_bootfb_register(dev, fallback);
}

device_t
wsdisplay_bootfb_native_device(void)
{
	KASSERT(KERNEL_LOCKED_P());
	if (!wsdisplay_bootfb.native || wsdisplay_bootfb.phase != BOOTFB_DEFERRED)
		return NULL;
	return wsdisplay_bootfb.device;
}

int
wsdisplay_bootfb_claim(device_t dev, struct wsdisplay_bootfb_lease **out)
{
	int error;

	if (out == NULL || dev == NULL)
		return EINVAL;
	error = wsdisplay_bootfb_context();
	if (error != 0)
		return error;
	KERNEL_LOCK(1, NULL);
	if (dev != wsdisplay_bootfb_native_device() ||
	    wsdisplay_bootfb.legacy_owner != NULL || wsdisplay_bootfb.committed ||
	    wsdisplay_bootfb_lease.issued) {
		error = EBUSY;
	} else if (!wsdisplay_bootfb_binding()) {
		wsdisplay_bootfb.phase = BOOTFB_RETAINED;
		error = ENXIO;
	} else {
		wsdisplay_bootfb.phase = BOOTFB_CLAIMED;
		wsdisplay_bootfb_lease.issued = true;
		*out = &wsdisplay_bootfb_lease;
		error = 0;
	}
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

device_t
wsdisplay_bootfb_lease_device(struct wsdisplay_bootfb_lease *lease)
{
	KASSERT(KERNEL_LOCKED_P());
	if (lease != &wsdisplay_bootfb_lease || !wsdisplay_bootfb_lease.issued ||
	    wsdisplay_bootfb.phase != BOOTFB_CLAIMED)
		return NULL;
	return wsdisplay_bootfb.device;
}

void
wsdisplay_bootfb_retain(struct wsdisplay_bootfb_lease *lease)
{
	KASSERT(KERNEL_LOCKED_P());
	if (wsdisplay_bootfb_lease_device(lease) != NULL)
		wsdisplay_bootfb.phase = BOOTFB_RETAINED;
}

int
wsdisplay_bootfb_cancel(struct wsdisplay_bootfb_lease *lease)
{
	int error;

	error = wsdisplay_bootfb_context();
	if (error != 0)
		return error;
	KERNEL_LOCK(1, NULL);
	if (wsdisplay_bootfb_lease_device(lease) == NULL) {
		error = EPERM;
	} else if (!wsdisplay_bootfb_binding()) {
		wsdisplay_bootfb.phase = BOOTFB_RETAINED;
		error = ENXIO;
	} else {
		wsdisplay_bootfb.phase = BOOTFB_DEFERRED;
		if (wsdisplay_bootfb.pending)
			(void)wsdisplay_bootfb_finalize(wsdisplay_bootfb.device);
		error = 0;
	}
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}
#ifdef WSDISPLAY_BOOTFB_HANDOFF
static bool
wsdisplay_bootfb_issued(struct wsdisplay_bootfb_lease *lease)
{

	KASSERT(KERNEL_LOCKED_P());
	return lease == &wsdisplay_bootfb_lease &&
	    wsdisplay_bootfb_lease.issued;
}

int
wsdisplay_bootfb_close_status(struct wsdisplay_bootfb_lease *lease,
    struct wsdisplay_bootfb_close_status *out)
{
	int error = 0;

	if (out == NULL)
		return EINVAL;
	if (cpu_intr_p() || cpu_softintr_p())
		return EWOULDBLOCK;
	ASSERT_SLEEPABLE();
	KERNEL_LOCK(1, NULL);
	if (!wsdisplay_bootfb_issued(lease))
		error = EPERM;
	else
		memcpy(out, &lease->close, sizeof(*out));
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

int
wsdisplay_bootfb_serial_closed(struct wsdisplay_bootfb_lease *lease)
{
	int error;

	if (pserialize_in_read_section())
		return EDEADLK;
	error = wsdisplay_bootfb_context();
	if (error != 0)
		return error;
	if (cold)
		return EWOULDBLOCK;
	KERNEL_LOCK(1, NULL);
	if (!wsdisplay_bootfb_issued(lease) ||
	    wsdisplay_bootfb.phase != BOOTFB_CLOSED) {
		error = EPERM;
	} else if (lease->close.error != 0) {
		error = lease->close.error;
	} else if (!lease->close.attempted || !lease->close.closed ||
	    !lease->close.drained) {
		error = ENXIO;
	} else {
		error = wsdisplay_early_serial_validate(lease);
	}
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

int
wsdisplay_bootfb_close_serial(struct wsdisplay_bootfb_lease *lease)
{
	struct wsdisplay_bootfb_close_status value;
	pserialize_t psz;
	int error;

	if (pserialize_in_read_section())
		return EDEADLK;
	error = wsdisplay_bootfb_context();
	if (error != 0)
		return error;
	if (cold)
		return EWOULDBLOCK;
	KERNEL_LOCK(1, NULL);
	if (!wsdisplay_bootfb_issued(lease)) {
		error = EPERM;
		goto out;
	}
	if (wsdisplay_bootfb.phase == BOOTFB_CLOSED) {
		error = wsdisplay_early_serial_validate(lease);
		if (error == 0)
			error = lease->close.error;
		goto out;
	}
	if (wsdisplay_bootfb.phase != BOOTFB_CLAIMED) {
		error = EBUSY;
		goto out;
	}
	error = wsdisplay_early_serial_prepare(wsdisplay_bootfb.type,
	    wsdisplay_bootfb.cookie, lease);
	if (error != 0)
		goto out;
	/* Refuse concurrent cancel while KM_SLEEP or the barrier drops Giant. */
	wsdisplay_bootfb.phase = BOOTFB_CLOSING;
	KERNEL_UNLOCK_ONE(NULL);
	psz = pserialize_create();
	KERNEL_LOCK(1, NULL);
	memset(&value, 0, sizeof(value));
	error = wsdisplay_early_serial_validate(lease);
	if (error == 0)
		error = wsdisplay_early_close(wsdisplay_bootfb.type,
		    wsdisplay_bootfb.cookie, psz, &value);
	if (!value.closed)
		wsdisplay_early_serial_cancel(lease);
	else if (error == 0)
		error = wsdisplay_early_serial_validate(lease);
	value.error = error;
	memcpy(&lease->close, &value, sizeof(value));
	wsdisplay_bootfb.phase = value.closed ? BOOTFB_CLOSED : BOOTFB_CLAIMED;
	KERNEL_UNLOCK_ONE(NULL);
	pserialize_destroy(psz);
	return error;
out:
	KERNEL_UNLOCK_ONE(NULL);
	return error;
}

int
wsdisplay_bootfb_close(struct wsdisplay_bootfb_lease *lease)
{
	struct wsdisplay_bootfb_close_status value;
	pserialize_t psz = NULL;
	int error;

	if (pserialize_in_read_section())
		return EDEADLK;
	error = wsdisplay_bootfb_context();
	if (error != 0)
		return error;
	KERNEL_LOCK(1, NULL);
again:
	if (!wsdisplay_bootfb_issued(lease)) {
		error = EPERM;
	} else if (wsdisplay_bootfb.phase == BOOTFB_CLOSED) {
		error = lease->close.error;
	} else if (wsdisplay_bootfb.phase != BOOTFB_CLAIMED) {
		error = EBUSY;
	} else if (psz == NULL) {
		/* KM_SLEEP can yield Giant. Revalidate after allocation. */
		KERNEL_UNLOCK_ONE(NULL);
		psz = pserialize_create();
		KERNEL_LOCK(1, NULL);
		goto again;
	} else {
		memset(&value, 0, sizeof(value));
		wsdisplay_bootfb.phase = BOOTFB_CLOSING;
		error = wsdisplay_early_close(wsdisplay_bootfb.type,
		    wsdisplay_bootfb.cookie, psz, &value);
		value.error = error;
		memcpy(&lease->close, &value, sizeof(value));
		wsdisplay_bootfb.phase = value.closed ?
		    BOOTFB_CLOSED : BOOTFB_CLAIMED;
	}
	KERNEL_UNLOCK_ONE(NULL);
	if (psz != NULL)
		pserialize_destroy(psz);
	return error;
}

#endif

#endif
