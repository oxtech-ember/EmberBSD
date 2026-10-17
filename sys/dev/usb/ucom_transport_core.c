/* Origin: EmberBSD opt-in ucom transport state core, 2026-10-09. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/errno.h>
#include <sys/null.h>
#else
#include <errno.h>
#endif

#include "ucom_transport_core.h"

void
ucom_transport_core_init(struct ucom_transport_core *s)
{

	s->state = UCT_CLOSED;
	s->epoch = 0;
	s->cookie = 0;
	s->tx_length = 0;
	s->tx_pending = 0;
	s->error = 0;
}

int
ucom_transport_core_open(struct ucom_transport_core *s, uint64_t *epoch)
{

	if (epoch == NULL)
		return EINVAL;
	*epoch = 0;
	if (s == NULL)
		return EINVAL;
	if (s->state == UCT_DETACHED)
		return ENXIO;
	if (s->state == UCT_OPEN)
		return EBUSY;
	if (s->state != UCT_CLOSED)
		return EIO;
	if (s->epoch == (uint64_t)-1) {
		s->state = UCT_DETACHED;
		s->tx_pending = 0;
		s->tx_length = 0;
		s->error = EOVERFLOW;
		return EOVERFLOW;
	}
	s->epoch++;
	s->cookie = 0;
	s->tx_length = 0;
	s->tx_pending = 0;
	s->error = 0;
	s->state = UCT_OPEN;
	*epoch = s->epoch;
	return 0;
}

void
ucom_transport_core_close(struct ucom_transport_core *s)
{

	if (s->state == UCT_DETACHED)
		return;
	s->state = UCT_CLOSED;
	s->tx_length = 0;
	s->tx_pending = 0;
	s->error = 0;
}

void
ucom_transport_core_detach(struct ucom_transport_core *s)
{

	if (s->state == UCT_DETACHED)
		return;
	s->state = UCT_DETACHED;
	s->tx_length = 0;
	s->tx_pending = 0;
	s->error = ENXIO;
}

int
ucom_transport_core_accept(const struct ucom_transport_core *s, uint64_t epoch)
{

	return s != NULL && s->state == UCT_OPEN && s->epoch == epoch;
}

int
ucom_transport_core_begin(struct ucom_transport_core *s, size_t len,
    uint64_t *cookie)
{

	if (cookie == NULL)
		return EINVAL;
	*cookie = 0;
	if (s == NULL || len == 0)
		return EINVAL;
	if (s->state == UCT_DETACHED)
		return ENXIO;
	if (s->state != UCT_OPEN)
		return EIO;
	if (s->tx_pending)
		return EBUSY;
	if (s->cookie == (uint64_t)-1) {
		ucom_transport_core_fault(s, EOVERFLOW);
		return EOVERFLOW;
	}
	s->cookie++;
	s->tx_length = len;
	s->tx_pending = 1;
	*cookie = s->cookie;
	return 0;
}

int
ucom_transport_core_done(struct ucom_transport_core *s, uint64_t epoch,
    uint64_t cookie, size_t actual, int error, size_t *consumed)
{

	if (consumed == NULL)
		return 0;
	*consumed = 0;
	if (!ucom_transport_core_accept(s, epoch) || !s->tx_pending ||
	    s->cookie != cookie)
		return 0;
	if (error != 0 || actual != s->tx_length) {
		ucom_transport_core_fault(s, error > 0 ? error : EIO);
		return 1;
	}
	*consumed = s->tx_length;
	s->tx_length = 0;
	s->tx_pending = 0;
	return 1;
}

void
ucom_transport_core_fault(struct ucom_transport_core *s, int error)
{

	if (s->state != UCT_OPEN && s->state != UCT_FAULT)
		return;
	s->state = UCT_FAULT;
	s->tx_length = 0;
	s->tx_pending = 0;
	s->error = error > 0 ? error : EIO;
}
