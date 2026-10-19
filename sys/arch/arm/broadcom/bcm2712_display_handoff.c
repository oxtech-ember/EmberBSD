/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Protocol facts: Raspberry Pi Linux a1a601d4857978d810175365550c4536701df4ed,
 * vc4_drv.c:391-405, raspberrypi-firmware.h:100 and raspberrypi.c:154-184.
 * No Linux implementation code is copied here. No native invocation exists.
 */
#include <sys/cdefs.h>
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif
#include "bcm2712_display_handoffvar.h"

static uint32_t
bcm2712_handoff_word(const uint8_t *packet)
{

	return (uint32_t)packet[0] | ((uint32_t)packet[1] << 8) |
	    ((uint32_t)packet[2] << 16) | ((uint32_t)packet[3] << 24);
}

int
bcm2712_display_notify_done(bcm2712_display_handoff_exchange_fn exchange,
    void *arg, struct bcm2712_display_handoff_status *status)
{
	_Alignas(16) uint8_t packet[24] = { 24, [8] = 0x66, [10] = 0x03 };
	uint32_t response;
	int error;

	if (exchange == NULL || status == NULL)
		return EINVAL;
	if (status->attempted || status->complete || status->fault != 0)
		return EBUSY;
	status->attempted = true;
	error = exchange(arg, packet, sizeof(packet));
	if (error == 0) {
		response = bcm2712_handoff_word(packet + 16);
		if (bcm2712_handoff_word(packet) != 24 ||
		    bcm2712_handoff_word(packet + 4) != 0x80000000U ||
		    bcm2712_handoff_word(packet + 8) != 0x00030066U ||
		    bcm2712_handoff_word(packet + 12) != 0 ||
		    bcm2712_handoff_word(packet + 20) != 0)
			error = EIO;
		else if (response == 0)
			error = EOPNOTSUPP;
		else if (response != 0x80000000U)
			error = EIO;
	}
	status->fault = error;
	if (error == 0)
		status->complete = true;
	return error;
}
