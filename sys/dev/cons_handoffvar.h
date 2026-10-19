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
#endif
#endif
