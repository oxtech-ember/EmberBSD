/* Origin: EmberBSD resident PLCOM console readiness. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _EVBARM_DEV_PLCOM_CONSOLEVAR_H_
#define _EVBARM_DEV_PLCOM_CONSOLEVAR_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/device.h>

#ifdef WSDISPLAY_BOOTFB_HANDOFF
struct consdev;
struct plcom_softc;
struct plcom_console_ready;

/* Selected frontend only. Refuse replacement before changing storage or maps. */
bool plcom_console_attach_begin(device_t, bus_space_tag_t, bus_addr_t)
    __attribute__((__warn_unused_result__));
/* Frontend only. Call after its real hard interrupt establishment succeeds. */
void plcom_console_attach_complete(struct plcom_softc *, void *);

/*
 * Private boot consumer. Warm process context with Giant held throughout.
 * The opaque record remains resident. Revocation is permanent until reboot.
 * Success describes a real attached tty; it does not pin a routing operation.
 * Revalidate after dropping Giant. Neither call changes console or hardware.
 * An error leaves caller output unchanged. Compare previous by identity only.
 */
int plcom_console_ready_get(const struct plcom_console_ready **)
    __attribute__((__warn_unused_result__));
int plcom_console_ready_validate(const struct plcom_console_ready *,
    const struct consdev *, dev_t *) __attribute__((__warn_unused_result__));
#endif
#endif
