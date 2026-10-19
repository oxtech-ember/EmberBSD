/* Origin: EmberBSD private boot framebuffer fallback issuer. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_WSCONS_WSDISPLAY_BOOTFBVAR_H_
#define _DEV_WSCONS_WSDISPLAY_BOOTFBVAR_H_

#include <sys/types.h>
#include <sys/device.h>

struct wsscreen_descr;
struct wsdisplay_bootfb_lease;

/* Built-in boot frontend only. Callback/device text must remain resident. */
int wsdisplay_bootfb_register_legacy(device_t, int (*)(device_t));
int wsdisplay_bootfb_fallback(device_t, int (*)(device_t));
bool wsdisplay_bootfb_reserve(device_t);
device_t wsdisplay_bootfb_legacy_device(device_t);
bool wsdisplay_bootfb_release(device_t);
void wsdisplay_bootfb_commit(device_t);

#ifdef WSDISPLAY_BOOTFB_DEFER
/* Claim identifies the resident binding. It grants no hardware access. */
bool wsdisplay_early_binding_matches(const struct wsscreen_descr *, void *);
int wsdisplay_bootfb_defer(device_t, const struct wsscreen_descr *, void *,
    int (*)(device_t));
device_t wsdisplay_bootfb_native_device(void);
int wsdisplay_bootfb_claim(device_t, struct wsdisplay_bootfb_lease **);
device_t wsdisplay_bootfb_lease_device(struct wsdisplay_bootfb_lease *);
int wsdisplay_bootfb_cancel(struct wsdisplay_bootfb_lease *);
void wsdisplay_bootfb_retain(struct wsdisplay_bootfb_lease *);
#ifdef WSDISPLAY_BOOTFB_HANDOFF
#include <sys/pserialize.h>
/* Private value facts. Neither closed nor drained grants hardware access. */
struct wsdisplay_bootfb_close_status {
	uint64_t generation;
	int error;
	bool attempted;
	bool closed;
	bool drained;
};
/* Exact successful serial CPU close only. This grants no hardware authority. */
int wsdisplay_bootfb_serial_closed(struct wsdisplay_bootfb_lease *)
    __attribute__((__warn_unused_result__));
/* Built-in boot transaction. Exact lease only; pre-freeze failures may cancel. */
int wsdisplay_bootfb_close_serial(struct wsdisplay_bootfb_lease *)
    __attribute__((__warn_unused_result__));
int wsdisplay_early_serial_prepare(const struct wsscreen_descr *, void *,
    struct wsdisplay_bootfb_lease *) __attribute__((__warn_unused_result__));
int wsdisplay_early_serial_validate(struct wsdisplay_bootfb_lease *)
    __attribute__((__warn_unused_result__));
void wsdisplay_early_serial_cancel(struct wsdisplay_bootfb_lease *);
int wsdisplay_bootfb_close(struct wsdisplay_bootfb_lease *)
    __attribute__((__warn_unused_result__));
int wsdisplay_bootfb_close_status(struct wsdisplay_bootfb_lease *,
    struct wsdisplay_bootfb_close_status *)
    __attribute__((__warn_unused_result__));
/* Issuer-only: Giant held on entry and return; all of it drops at the barrier. */
int wsdisplay_early_close(const struct wsscreen_descr *, void *, pserialize_t,
    struct wsdisplay_bootfb_close_status *)
    __attribute__((__warn_unused_result__));
#endif
#endif
#endif
