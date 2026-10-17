/* Origin: EmberBSD bounded external ucom RX queue, 2026-10-10. */
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
#include "ucom_transport_rx.h"

void
ucom_transport_rx_init(struct ucom_transport_rx *q)
{

	memset(q, 0, sizeof(*q));
}

int
ucom_transport_rx_put(struct ucom_transport_rx *q, const void *data, size_t len)
{
	size_t tail, first;

	if (q == NULL || (data == NULL && len != 0))
		return EINVAL;
	if (len > UCOM_TRANSPORT_RX_CAPACITY - q->used)
		return ENOBUFS;
	if (len == 0)
		return 0;
	tail = (q->head + q->used) % UCOM_TRANSPORT_RX_CAPACITY;
	first = UCOM_TRANSPORT_RX_CAPACITY - tail;
	if (first > len)
		first = len;
	memcpy(q->data + tail, data, first);
	if (len > first)
		memcpy(q->data, (const uint8_t *)data + first, len - first);
	q->used += len;
	if (q->used >= UCOM_TRANSPORT_RX_HIGH)
		q->paused = 1;
	return 0;
}

const uint8_t *
ucom_transport_rx_peek(const struct ucom_transport_rx *q, size_t *len)
{

	if (len == NULL)
		return NULL;
	*len = 0;
	if (q == NULL || q->used == 0)
		return NULL;
	*len = UCOM_TRANSPORT_RX_CAPACITY - q->head;
	if (*len > q->used)
		*len = q->used;
	return q->data + q->head;
}

int
ucom_transport_rx_drop(struct ucom_transport_rx *q, size_t len)
{

	if (q == NULL || len > q->used)
		return EINVAL;
	q->head = (q->head + len) % UCOM_TRANSPORT_RX_CAPACITY;
	q->used -= len;
	if (q->used <= UCOM_TRANSPORT_RX_LOW)
		q->paused = 0;
	return 0;
}
