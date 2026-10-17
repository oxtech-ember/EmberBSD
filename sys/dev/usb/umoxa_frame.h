/* Origin: EmberBSD MOXA G2 wire framing helpers, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _DEV_USB_UMOXA_FRAME_H_
#define _DEV_USB_UMOXA_FRAME_H_

#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

/* Callbacks borrow payload/detail spans for the duration of the call. */
typedef void (*umoxa_data_fn)(void *, uint16_t, const uint8_t *, size_t);
typedef void (*umoxa_event_fn)(void *, uint16_t, uint16_t, const uint8_t *);

/* RX input must remain immutable, including during callbacks. */
int umoxa_g2_rx_data(const uint8_t *, size_t, size_t, umoxa_data_fn, void *);
int umoxa_g2_rx_events(const uint8_t *, size_t, umoxa_event_fn, void *);

/* Payload/output may overlap; written must not overlap either span. */
int umoxa_g2_tx(uint16_t, const uint8_t *, size_t, int, uint8_t *, size_t,
    size_t *);

#endif /* _DEV_USB_UMOXA_FRAME_H_ */
