/* Origin: EmberBSD checked console publication. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_CONS_HANDOFFVAR_H_
#define _DEV_CONS_HANDOFFVAR_H_

#include <sys/types.h>

/*
 * Count active publications and admitted input sessions. This does not serialize
 * their binding updates.
 * Supported runtime binders also hold Giant. The selected cold preattach path
 * uses bootstrap serialization. Freeze closes admission, not existing readers.
 * The present-lease actuator separately performs the real reader barrier.
 */
#ifdef WSDISPLAY_BOOTFB_HANDOFF
int cn_publication_enter(void) __attribute__((__warn_unused_result__));
void cn_publication_exit(void);
int cn_publication_freeze(void) __attribute__((__warn_unused_result__));
bool cn_publication_closed(void);

struct consdev;
struct wsdisplay_bootfb_lease;
/* Boot-only check. No console vnode or redirected tty may already be in use. */
int cn_bootfb_console_unused(void) __attribute__((__warn_unused_result__));
/*
 * Fixed built-in PLCOM ACPI provider. This profile requires its real definition.
 * Giant is held. The exact live lease and MI-recorded previous backend are used.
 * Hold survives allocation and the reader barrier. Closed holds cannot release.
 * No registration, fallback provider or hardware authority is supplied.
 */
int cn_bootfb_serial_hold(struct wsdisplay_bootfb_lease *,
    const struct consdev *, dev_t *) __attribute__((__warn_unused_result__));
int cn_bootfb_serial_validate(struct wsdisplay_bootfb_lease *,
    const struct consdev *, dev_t *) __attribute__((__warn_unused_result__));
void cn_bootfb_serial_release(struct wsdisplay_bootfb_lease *);
#endif
#endif
