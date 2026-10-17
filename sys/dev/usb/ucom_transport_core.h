/* Origin: EmberBSD opt-in ucom transport state core, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _DEV_USB_UCOM_TRANSPORT_CORE_H_
#define _DEV_USB_UCOM_TRANSPORT_CORE_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

enum ucom_transport_state {
	UCT_CLOSED, UCT_OPEN, UCT_FAULT, UCT_DETACHED
};

/* Private to one port, initialized before use, protected by caller's lock. */
struct ucom_transport_core {
	enum ucom_transport_state state;
	uint64_t epoch;
	uint64_t cookie;
	size_t tx_length;
	int tx_pending;
	int error;
};

/* Output counters must not alias the state. Void APIs require non-NULL state. */
void ucom_transport_core_init(struct ucom_transport_core *);
int ucom_transport_core_open(struct ucom_transport_core *, uint64_t *);
void ucom_transport_core_close(struct ucom_transport_core *);
void ucom_transport_core_detach(struct ucom_transport_core *);
int ucom_transport_core_accept(const struct ucom_transport_core *, uint64_t);
int ucom_transport_core_begin(struct ucom_transport_core *, size_t, uint64_t *);
int ucom_transport_core_done(struct ucom_transport_core *, uint64_t, uint64_t,
    size_t, int, size_t *);
void ucom_transport_core_fault(struct ucom_transport_core *, int);

#endif /* _DEV_USB_UCOM_TRANSPORT_CORE_H_ */
