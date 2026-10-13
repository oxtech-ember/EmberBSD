/* Origin: EmberBSD UPort 1150 protocol contracts, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DEV_USB_UMOXA1150_PROTOCOL_H_
#define _DEV_USB_UMOXA1150_PROTOCOL_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#include <stddef.h>
#endif
#define UMOXA1150_IMAGE_SIZE 16287
struct umoxa1150_settings {
 uint32_t baud;
 uint8_t bits, parity, stops, mode, rtscts, ixon, ixoff, send_break, xon, xoff;
};
struct umoxa1150_endpoint { uint8_t address, type; uint16_t packet; };
struct umoxa1150_layout { uint8_t in, out, interrupt; uint16_t interrupt_size; };
struct umoxa1150_event { uint8_t kind, lsr, msr; };
struct umoxa1150_io {
 void *arg;
 int (*control)(void *, uint8_t, uint16_t, uint16_t, const uint8_t *, size_t);
 int (*bulk)(void *, const uint8_t *, size_t, size_t *);
};
int umoxa1150_config(const struct umoxa1150_settings *, uint8_t [10]);
int umoxa1150_firmware(const uint8_t *, size_t, uint8_t *, size_t);
int umoxa1150_event(const uint8_t *, size_t, struct umoxa1150_event *);
int umoxa1150_endpoints(const struct umoxa1150_endpoint *, size_t, int,
    struct umoxa1150_layout *);
int umoxa1150_apply(const struct umoxa1150_io *, const struct umoxa1150_settings *, uint8_t);
int umoxa1150_start(const struct umoxa1150_io *, const struct umoxa1150_settings *, uint8_t);
int umoxa1150_stop(const struct umoxa1150_io *);
int umoxa1150_upload(const struct umoxa1150_io *, const uint8_t *, size_t);
#endif
