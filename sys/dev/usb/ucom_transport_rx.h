/* Origin: EmberBSD bounded external ucom RX queue, 2026-10-10. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _DEV_USB_UCOM_TRANSPORT_RX_H_
#define _DEV_USB_UCOM_TRANSPORT_RX_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define UCOM_TRANSPORT_RX_CAPACITY 8192
#define UCOM_TRANSPORT_RX_HIGH 6144
#define UCOM_TRANSPORT_RX_LOW 2048

/* Caller owns locking; valid spans/counters never alias the queue. */
struct ucom_transport_rx {
	size_t head;
	size_t used;
	int paused;
	uint8_t data[UCOM_TRANSPORT_RX_CAPACITY];
};

void ucom_transport_rx_init(struct ucom_transport_rx *);
int ucom_transport_rx_put(struct ucom_transport_rx *, const void *, size_t);
const uint8_t *ucom_transport_rx_peek(const struct ucom_transport_rx *, size_t *);
int ucom_transport_rx_drop(struct ucom_transport_rx *, size_t);

#endif /* _DEV_USB_UCOM_TRANSPORT_RX_H_ */
