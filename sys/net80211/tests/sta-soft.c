/* SPDX-License-Identifier: BSD-2-Clause */
/* Contract fixture for the host-MLME BTM Request admission rule. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ieee80211_sta_soft.h"

static const uint8_t bss[6] = { 0x50, 0xff, 0x20, 0x00, 0x00, 0x01 };
static const uint8_t own[6] = { 0xb0, 0x8c, 0xb3, 0x00, 0x00, 0x02 };
static const uint8_t other[6] = { 0x50, 0xff, 0x20, 0x00, 0x00, 0x03 };
static unsigned checks;

static bool
admit(const uint8_t *a1, const uint8_t *a2, const uint8_t *a3, uint8_t fc1,
    uint8_t seq0, const uint8_t *body, size_t len)
{
	checks++;
	return ieee80211_sta_soft_rx_admit(bss, own, a1, a2, a3, fc1, seq0,
	    body, len);
}

int
main(void)
{
	/* Category 10, BTM Request, token, mode, timer, validity. */
	uint8_t req[7] = { 10, 7, 1, 0x01, 0, 0, 255 };
	uint8_t body[7];
	unsigned fc1, i;

	assert(admit(own, bss, bss, 0, 0, req, sizeof(req)));
	/* Retry, Power Management and More Data do not change the frame. */
	for (fc1 = 0; fc1 < 256; fc1++) {
		bool ok = (fc1 & 0xc7) == 0;

		assert(admit(own, bss, bss, fc1, 0, req, sizeof(req)) == ok);
	}
	/* A fragment number is never valid for an Action frame here. */
	for (i = 1; i < 16; i++)
		assert(!admit(own, bss, bss, 0, i, req, sizeof(req)));
	/* The sequence number high nibble is free. */
	assert(admit(own, bss, bss, 0, 0xf0, req, sizeof(req)));
	/* Only the station, from its own BSS. */
	assert(!admit(other, bss, bss, 0, 0, req, sizeof(req)));
	assert(!admit(own, other, bss, 0, 0, req, sizeof(req)));
	assert(!admit(own, bss, other, 0, 0, req, sizeof(req)));
	/* Only a complete BTM Request. */
	for (i = 0; i < sizeof(req); i++)
		assert(!admit(own, bss, bss, 0, 0, req, i));
	assert(!admit(own, bss, bss, 0, 0, NULL, sizeof(req)));
	for (i = 0; i < 256; i++) {
		memcpy(body, req, sizeof(body));
		body[1] = (uint8_t)i;
		assert(admit(own, bss, bss, 0, 0, body, sizeof(body)) == (i == 7));
		memcpy(body, req, sizeof(body));
		body[0] = (uint8_t)i;
		assert(admit(own, bss, bss, 0, 0, body, sizeof(body)) == (i == 10));
	}
	printf("PASS: %u host-MLME BTM admission checks\n", checks);
	return 0;
}
