/* Origin: EmberBSD independent MOXA G2 control/firmware contracts, 2026-10-13. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _DEV_USB_UMOXA_G2_PROTOCOL_H_
#define _DEV_USB_UMOXA_G2_PROTOCOL_H_
#ifdef _KERNEL
#include <sys/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif
#define UMOXA_G2_FIRMWARE_MAX (256 * 1024)
struct umoxa_g2_settings {
	uint32_t baud;
	uint8_t bits, parity, stops, mode, rtscts, mcr;
};
struct umoxa_g2_endpoint { uint8_t address, type; uint16_t packet; };
struct umoxa_g2_layout { uint8_t data_in, event_in, out; uint16_t packet; };
/* control returns errno; its adapter requires exactly size bytes on success. */
struct umoxa_g2_io {
	void *arg;
	int (*control)(void *, int, uint8_t, uint16_t, uint16_t, uint8_t *, size_t);
	void (*delay)(void *, unsigned);
};
int umoxa_g2_config(const struct umoxa_g2_settings *, uint8_t[4], uint8_t[4]);
int umoxa_g2_endpoints(const struct umoxa_g2_endpoint *, size_t, struct umoxa_g2_layout *);
int umoxa_g2_probe(const struct umoxa_g2_io *, uint32_t *, int *);
int umoxa_g2_firmware_version(const uint8_t *, size_t, uint32_t *);
/* Full transfer and STOP acknowledgement are distinct; readiness is unproven. */
int umoxa_g2_download(const struct umoxa_g2_io *, const uint8_t *, size_t, int *);
int umoxa_g2_quiesce(const struct umoxa_g2_io *, unsigned);
int umoxa_g2_prepare(const struct umoxa_g2_io *, unsigned, const struct umoxa_g2_settings *);
int umoxa_g2_enable(const struct umoxa_g2_io *, unsigned);
int umoxa_g2_set(const struct umoxa_g2_io *, unsigned, int, int, const struct umoxa_g2_settings *);
#endif
