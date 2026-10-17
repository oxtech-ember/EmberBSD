/* Origin: EmberBSD MOXA G2 wire framing helpers, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/systm.h>
#else
#include <errno.h>
#include <string.h>
#endif

#include "umoxa_frame.h"

static uint16_t
umoxa_be16(const uint8_t *p)
{

	return ((uint16_t)p[0] << 8) | p[1];
}

int
umoxa_g2_rx_data(const uint8_t *data, size_t len, size_t limit,
    umoxa_data_fn emit, void *arg)
{
	size_t pos, payload;
	uint16_t port;

	if ((data == NULL && len != 0) || limit == 0 || limit > 65535 ||
	    emit == NULL)
		return EINVAL;
	/* Validate the entire transfer before publishing any of its records. */
	for (pos = 0; pos < len; pos += payload) {
		if (len - pos < 4)
			return EPROTO;
		port = umoxa_be16(data + pos);
		payload = umoxa_be16(data + pos + 2);
		pos += 4;
		if (port >= 4 || payload > limit || payload > len - pos)
			return EPROTO;
	}
	for (pos = 0; pos < len; pos += payload) {
		port = umoxa_be16(data + pos);
		payload = umoxa_be16(data + pos + 2);
		pos += 4;
		if (payload != 0)
			emit(arg, port, data + pos, payload);
	}
	return 0;
}

int
umoxa_g2_rx_events(const uint8_t *data, size_t len, umoxa_event_fn emit,
    void *arg)
{
	size_t pos;

	if ((data == NULL && len != 0) || emit == NULL)
		return EINVAL;
	if (len % 8 != 0)
		return EPROTO;
	for (pos = 0; pos < len; pos += 8) {
		if (umoxa_be16(data + pos) >= 4)
			return EPROTO;
	}
	for (pos = 0; pos < len; pos += 8)
		emit(arg, umoxa_be16(data + pos), umoxa_be16(data + pos + 2),
		    data + pos + 4);
	return 0;
}

int
umoxa_g2_tx(uint16_t port, const uint8_t *data, size_t len, int send_next,
    uint8_t *out, size_t capacity, size_t *written)
{

	if (written == NULL)
		return EINVAL;
	*written = 0;
	if (port >= 4 || data == NULL || out == NULL || len == 0 || len > 65535 ||
	    (send_next != 0 && send_next != 1))
		return EINVAL;
	if (capacity < len + 4)
		return EMSGSIZE;
	/* Move first: the payload may overlap the destination header. */
	memmove(out + 4, data, len);
	out[0] = send_next ? 0x80 : 0;
	out[1] = (uint8_t)port;
	out[2] = (uint8_t)(len >> 8);
	out[3] = (uint8_t)len;
	*written = len + 4;
	return 0;
}
