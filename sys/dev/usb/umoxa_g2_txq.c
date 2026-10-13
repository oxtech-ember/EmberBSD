/* Origin: EmberBSD bounded shared MOXA G2 transmit engine, 2026-10-10. */
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
#include "umoxa_g2_txq.h"

static uint32_t
umoxa_g2_fault_port(struct umoxa_g2_txq *q, unsigned int port, int error)
{
	struct umoxa_g2_port *p = &q->ports[port];

	if (p->state != UMOXA_OPEN)
		return 0;
	p->state = UMOXA_FAULT;
	p->job = UMOXA_EMPTY;
	p->len = 0;
	p->waiting = 0;
	p->deadline = 0;
	p->error = error > 0 ? error : EIO;
	return (uint32_t)1 << port;
}

uint32_t
umoxa_g2_txq_fault_all(struct umoxa_g2_txq *q, int error)
{
	uint32_t mask = 0;
	unsigned int port;

	if (q == NULL)
		return 0;
	for (port = 0; port < UMOXA_G2_PORTS; port++)
		mask |= umoxa_g2_fault_port(q, port, error);
	return mask;
}

uint32_t
umoxa_g2_txq_fault_port(struct umoxa_g2_txq *q, unsigned int port, int error)
{
	if (q == NULL || port >= UMOXA_G2_PORTS)
		return 0;
	return umoxa_g2_fault_port(q, port, error);
}

int
umoxa_g2_txq_init(struct umoxa_g2_txq *q, size_t budget, uint64_t timeout)
{

	if (q == NULL || budget < 5 || budget > UMOXA_G2_FRAME_MAX || timeout == 0)
		return EINVAL;
	memset(q, 0, sizeof(*q));
	q->budget = budget;
	q->timeout = timeout;
	return 0;
}

int
umoxa_g2_txq_open(struct umoxa_g2_txq *q, unsigned int port, uint64_t epoch)
{
	struct umoxa_g2_port *p;

	if (q == NULL || port >= UMOXA_G2_PORTS || epoch == 0)
		return EINVAL;
	p = &q->ports[port];
	if (p->state != UMOXA_CLOSED || (q->active && q->flight.port == port))
		return EBUSY;
	if (epoch <= p->last_epoch)
		return ESTALE;
	memset(p, 0, sizeof(*p));
	p->epoch = p->last_epoch = epoch;
	p->state = UMOXA_OPEN;
	return 0;
}

int
umoxa_g2_txq_close(struct umoxa_g2_txq *q, unsigned int port)
{
	struct umoxa_g2_port *p;
	uint64_t last;

	if (q == NULL || port >= UMOXA_G2_PORTS)
		return EINVAL;
	p = &q->ports[port];
	last = p->last_epoch;
	memset(p, 0, sizeof(*p));
	p->last_epoch = last;
	return 0;
}

int
umoxa_g2_txq_submit(struct umoxa_g2_txq *q, unsigned int port, uint64_t epoch,
    uint64_t cookie, const uint8_t *data, size_t len)
{
	struct umoxa_g2_port *p;

	if (q == NULL || port >= UMOXA_G2_PORTS || epoch == 0 || cookie == 0 ||
	    data == NULL || len == 0)
		return EINVAL;
	if (len > q->budget - 4)
		return EMSGSIZE;
	p = &q->ports[port];
	if (p->state == UMOXA_CLOSED)
		return ENXIO;
	if (p->state == UMOXA_FAULT)
		return p->error;
	if (epoch != p->epoch)
		return ESTALE;
	if (cookie <= p->last_cookie)
		return EALREADY;
	if (p->job != UMOXA_EMPTY)
		return EBUSY;
	memcpy(p->data, data, len);
	p->len = len;
	p->cookie = p->last_cookie = cookie;
	p->job = UMOXA_QUEUED;
	return 0;
}

int
umoxa_g2_txq_pick(struct umoxa_g2_txq *q, uint64_t now,
    struct umoxa_g2_xfer *x)
{
	struct umoxa_g2_port *p;
	unsigned int n, port;
	int send_next, error;
	size_t written;

	if (x == NULL)
		return EINVAL;
	memset(x, 0, sizeof(*x));
	if (q == NULL || now < q->now)
		return EINVAL;
	q->now = now;
	if (q->active)
		return EBUSY;
	for (n = 0; n < UMOXA_G2_PORTS; n++) {
		port = (q->cursor + n) % UMOXA_G2_PORTS;
		p = &q->ports[port];
		if (p->state != UMOXA_OPEN || p->job != UMOXA_QUEUED || p->waiting)
			continue;
		if (q->last_token == (uint64_t)-1 || now > (uint64_t)-1 - q->timeout) {
			x->fault_mask = umoxa_g2_txq_fault_all(q, EOVERFLOW);
			return EOVERFLOW;
		}
		send_next = p->sent >= UMOXA_G2_CREDIT_THRESHOLD;
		error = umoxa_g2_tx(port, p->data, p->len, send_next,
		    q->wire, q->budget, &written);
		if (error != 0) {
			x->fault_mask = umoxa_g2_txq_fault_all(q, error);
			return error;
		}
		if (send_next) {
			p->waiting = 1;
			p->deadline = now + q->timeout;
		}
		/* The wait gate bounds sent to threshold-1 + two payloads. */
		p->sent += p->len;
		p->job = UMOXA_INFLIGHT;
		q->flight.token = ++q->last_token;
		q->flight.epoch = p->epoch;
		q->flight.cookie = p->cookie;
		q->flight.port = port;
		q->flight.data = q->wire;
		q->flight.len = written;
		q->flight.fault_mask = 0;
		q->deadline = now + q->timeout;
		q->active = 1;
		q->cursor = (port + 1) % UMOXA_G2_PORTS;
		*x = q->flight;
		return 0;
	}
	return EAGAIN;
}

int
umoxa_g2_txq_done(struct umoxa_g2_txq *q, uint64_t token, size_t actual,
    int error, struct umoxa_g2_result *r)
{
	struct umoxa_g2_port *p;
	unsigned int port;

	if (r == NULL)
		return 0;
	memset(r, 0, sizeof(*r));
	if (q == NULL || !q->active || token == 0 || token != q->flight.token)
		return 0;
	port = q->flight.port;
	p = &q->ports[port];
	r->port = port;
	r->epoch = q->flight.epoch;
	r->cookie = q->flight.cookie;
	r->deliver = p->state == UMOXA_OPEN && p->job == UMOXA_INFLIGHT &&
	    p->epoch == r->epoch && p->cookie == r->cookie;
	if (error != 0) {
		r->error = error > 0 ? error : EIO;
		r->fault_mask = umoxa_g2_txq_fault_all(q, r->error);
	} else if (r->deliver && actual != q->flight.len) {
		r->error = actual > q->flight.len ? EPROTO : EIO;
		r->fault_mask = umoxa_g2_fault_port(q, port, r->error);
	} else if (r->deliver) {
		r->actual = q->flight.len - 4;
		p->job = UMOXA_EMPTY;
		p->len = 0;
	}
	/* Only a physical completion/abort makes the borrowed frame recyclable. */
	q->active = 0;
	q->deadline = 0;
	memset(&q->flight, 0, sizeof(q->flight));
	return 1;
}

static void
umoxa_g2_credit(void *arg, uint16_t port, uint16_t event,
    const uint8_t *detail)
{
	struct umoxa_g2_txq *q = arg;
	struct umoxa_g2_port *p = &q->ports[port];

	(void)detail;
	if (event == 2 && p->state == UMOXA_OPEN && p->waiting) {
		p->waiting = 0;
		p->sent = 0;
		p->deadline = 0;
	}
}

int
umoxa_g2_txq_events(struct umoxa_g2_txq *q, const uint8_t *data, size_t len,
    uint32_t *mask)
{
	int error;

	if (mask == NULL)
		return EINVAL;
	*mask = 0;
	if (q == NULL || (data == NULL && len != 0))
		return EINVAL;
	error = umoxa_g2_rx_events(data, len, umoxa_g2_credit, q);
	if (error != 0)
		*mask = umoxa_g2_txq_fault_all(q, error);
	return error;
}

int
umoxa_g2_txq_tick(struct umoxa_g2_txq *q, uint64_t now, uint32_t *mask)
{
	struct umoxa_g2_port *p;
	unsigned int port;

	if (mask == NULL)
		return EINVAL;
	*mask = 0;
	if (q == NULL || now < q->now)
		return EINVAL;
	q->now = now;
	if (q->active && now >= q->deadline) {
		*mask = umoxa_g2_txq_fault_all(q, ETIMEDOUT);
		return 0;
	}
	for (port = 0; port < UMOXA_G2_PORTS; port++) {
		p = &q->ports[port];
		if (p->state == UMOXA_OPEN && p->waiting && now >= p->deadline)
			*mask |= umoxa_g2_fault_port(q, port, ETIMEDOUT);
	}
	return 0;
}
