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
/* No close, writer drain, hardware handoff or access grant is implemented. */
bool wsdisplay_early_binding_matches(const struct wsscreen_descr *, void *);
int wsdisplay_bootfb_defer(device_t, const struct wsscreen_descr *, void *,
    int (*)(device_t));
device_t wsdisplay_bootfb_native_device(void);
int wsdisplay_bootfb_claim(device_t, struct wsdisplay_bootfb_lease **);
device_t wsdisplay_bootfb_lease_device(struct wsdisplay_bootfb_lease *);
int wsdisplay_bootfb_cancel(struct wsdisplay_bootfb_lease *);
void wsdisplay_bootfb_retain(struct wsdisplay_bootfb_lease *);
#endif
#endif
