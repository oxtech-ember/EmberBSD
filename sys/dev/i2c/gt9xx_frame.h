/* Origin: EmberBSD - decode Goodix GT9xx coordinate reports. */
/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _GT9XX_FRAME_H_
#define _GT9XX_FRAME_H_
#include <sys/types.h>
#ifndef _KERNEL
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#endif
struct gt9xx_contact {
	bool pressed;
	uint8_t id;
	uint16_t x, y;
};
int gt9xx_decode_frame(const uint8_t *, size_t, uint16_t, uint16_t,
    struct gt9xx_contact *);
#endif
