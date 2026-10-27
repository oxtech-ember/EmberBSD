/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Bounded copied-packet ownership. The native adapter owns the interlock. */
#ifdef _KERNEL
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#else
#include <errno.h>
#include <stddef.h>
#include <string.h>
#endif
#include <dev/ic/rtw88_async.h>

static int
same_epoch(const struct rtw88_async_epoch *a, const struct rtw88_async_epoch *b)
{
	return a->generation == b->generation && a->epoch == b->epoch &&
	    a->key_epoch == b->key_epoch;
}

static int
exhausted_epoch(const struct rtw88_async_epoch *id)
{
	return id->generation == UINT32_MAX || id->epoch == UINT64_MAX ||
	    id->key_epoch == UINT64_MAX;
}

static void
wake(struct rtw88_async *a)
{
	if (a->closed || a->total >= RTW88_ASYNC_SLOTS)
		return;
	for (unsigned q = 0; q < RTW88_NTXQ; q++) {
		uint32_t bit = UINT32_C(1) << q;

		if (a->count[q] > RTW88_ASYNC_LOW_WATER)
			continue;
		if ((a->blocked_control & bit) != 0) {
			a->blocked_control &= ~bit;
			a->wake_mask |= bit;
		}
		if ((a->blocked_data & bit) != 0 && a->data < RTW88_ASYNC_DATA) {
			a->blocked_data &= ~bit;
			a->wake_mask |= bit;
		}
	}
}

static void
release(struct rtw88_async *a, struct rtw88_async_slot *slot)
{
	a->count[slot->q]--;
	a->total--;
	if (!slot->control)
		a->data--;
	slot->state = RTW88_ASYNC_FREE;
	slot->stale = false;
	wake(a);
}

static struct rtw88_async_slot *
lookup(struct rtw88_async *a, const struct rtw88_async_ticket *ticket)
{
	struct rtw88_async_slot *slot;

	if (a == NULL || ticket == NULL || ticket->slot >= RTW88_ASYNC_SLOTS ||
	    ticket->serial == 0)
		return NULL;
	slot = &a->slots[ticket->slot];
	return slot->state != RTW88_ASYNC_FREE && slot->serial == ticket->serial ?
	    slot : NULL;
}

static int
expired(const struct rtw88_async_slot *slot, uint32_t now_ms)
{
	/* The first commit owns the timer; a retry never extends it. */
	return (uint32_t)(now_ms - slot->committed_ms) >= RTW88_ASYNC_DEADLINE_MS;
}

void
rtw88_async_init(struct rtw88_async *a, const struct rtw88_async_epoch *id)
{
	if (a == NULL)
		return;
	memset(a, 0, sizeof(*a));
	if (id == NULL) {
		a->closed = 1;
		a->error = EINVAL;
		return;
	}
	a->id = *id;
	if (exhausted_epoch(id)) {
		a->closed = 1;
		a->error = EOVERFLOW;
	}
}

int
rtw88_async_reserve(struct rtw88_async *a, const struct rtw88_async_epoch *id,
    unsigned q, bool control, struct rtw88_async_ticket *ticket,
    struct rtw88_tx_job **job)
{
	struct rtw88_async_slot *slot;
	unsigned index;

	if (a == NULL || id == NULL || ticket == NULL || job == NULL ||
	    q >= RTW88_NTXQ)
		return EINVAL;
	if (a->closed)
		return a->error;
	if (!same_epoch(id, &a->id))
		return ESTALE;
	if (a->serial == UINT64_MAX) {
		a->closed = 1;
		a->error = EOVERFLOW;
		return EOVERFLOW;
	}
	if (a->count[q] >= RTW88_ASYNC_PER_QUEUE ||
	    a->total >= RTW88_ASYNC_SLOTS || (!control && a->data >= RTW88_ASYNC_DATA)) {
		if (control)
			a->blocked_control |= UINT32_C(1) << q;
		else
			a->blocked_data |= UINT32_C(1) << q;
		return ENOBUFS;
	}
	for (index = 0; index < RTW88_ASYNC_SLOTS; index++)
		if (a->slots[index].state == RTW88_ASYNC_FREE)
			break;
	if (index == RTW88_ASYNC_SLOTS)
		return EPROTO;
	slot = &a->slots[index];
	/* Clear metadata only; the producer initializes its owned byte range. */
	memset(&slot->job, 0, offsetof(struct rtw88_tx_job, bytes));
	slot->owner = slot->job.id = *id;
	slot->q = slot->job.q = q;
	slot->control = slot->job.control = control;
	slot->serial = ++a->serial;
	slot->stale = false;
	slot->state = RTW88_ASYNC_RESERVED;
	a->count[q]++;
	a->total++;
	if (!control)
		a->data++;
	ticket->slot = index;
	ticket->serial = slot->serial;
	*job = &slot->job;
	return 0;
}

int
rtw88_async_commit(struct rtw88_async *a, const struct rtw88_async_ticket *ticket,
    size_t length, uint32_t now_ms)
{
	struct rtw88_async_slot *slot = lookup(a, ticket);
	unsigned q;

	if (a == NULL || ticket == NULL)
		return EINVAL;
	if (slot == NULL)
		return ESTALE;
	if (slot->state != RTW88_ASYNC_RESERVED)
		return EINVAL;
	if (a->closed || slot->stale || !same_epoch(&slot->owner, &a->id))
		return a->closed ? a->error : ESTALE;
	if (length == 0 || length > sizeof(slot->job.bytes))
		return EMSGSIZE;
	if (!same_epoch(&slot->job.id, &slot->owner) || slot->job.q != slot->q ||
	    slot->job.control != slot->control ||
	    (unsigned)slot->job.mode > RTW88_TX_MODE_AMPDU ||
	    (slot->job.mode == RTW88_TX_MODE_AUTO ? slot->job.operation > 1 :
	     slot->job.operation != 0))
		return EINVAL;
	q = slot->q;
	if (a->queued[q] >= RTW88_ASYNC_PER_QUEUE)
		return EPROTO;
	slot->job.length = length;
	slot->committed_ms = now_ms;
	a->fifo[q][a->tail[q]] = ticket->slot;
	a->tail[q] = (a->tail[q] + 1) % RTW88_ASYNC_PER_QUEUE;
	a->queued[q]++;
	slot->state = RTW88_ASYNC_QUEUED;
	return 0;
}

int
rtw88_async_cancel(struct rtw88_async *a, const struct rtw88_async_ticket *ticket)
{
	struct rtw88_async_slot *slot = lookup(a, ticket);

	if (a == NULL || ticket == NULL)
		return EINVAL;
	if (slot == NULL)
		return ESTALE;
	if (slot->state != RTW88_ASYNC_RESERVED)
		return EINVAL;
	release(a, slot);
	return 0;
}

int
rtw88_async_take(struct rtw88_async *a, uint32_t now_ms,
    struct rtw88_async_ticket *ticket, const struct rtw88_tx_job **job)
{
	if (a == NULL || ticket == NULL || job == NULL)
		return EINVAL;
	/* Admission can close while previously committed owners still exist. */
	for (unsigned n = 0; n < RTW88_NTXQ; n++) {
		unsigned q = (a->nextq + n) % RTW88_NTXQ;

		while (a->queued[q] != 0) {
			unsigned index = a->fifo[q][a->head[q]];
			struct rtw88_async_slot *slot = &a->slots[index];

			a->head[q] = (a->head[q] + 1) % RTW88_ASYNC_PER_QUEUE;
			a->queued[q]--;
			if (slot->stale || !same_epoch(&slot->owner, &a->id) ||
			    expired(slot, now_ms)) {
				release(a, slot);
				continue;
			}
			slot->state = RTW88_ASYNC_ACTIVE;
			a->nextq = (q + 1) % RTW88_NTXQ;
			ticket->slot = index;
			ticket->serial = slot->serial;
			*job = &slot->job;
			return 0;
		}
	}
	return a->closed ? a->error : EAGAIN;
}

int
rtw88_async_finish(struct rtw88_async *a, const struct rtw88_async_ticket *ticket,
    int error, enum rtw88_tx_progress progress, uint32_t now_ms)
{
	struct rtw88_async_slot *slot = lookup(a, ticket);
	unsigned q;

	if (a == NULL || ticket == NULL)
		return EINVAL;
	if (slot == NULL)
		return ESTALE;
	if (slot->state != RTW88_ASYNC_ACTIVE || error < 0 ||
	    (unsigned)progress > RTW88_TX_ACCEPTED ||
	    (error == 0 && progress != RTW88_TX_ACCEPTED))
		return EINVAL;
	if (error == EBUSY && progress == RTW88_TX_NOT_STARTED && !a->closed &&
	    !slot->stale && same_epoch(&slot->owner, &a->id) && !expired(slot, now_ms)) {
		q = slot->q;
		a->head[q] = (a->head[q] + RTW88_ASYNC_PER_QUEUE - 1) % RTW88_ASYNC_PER_QUEUE;
		a->fifo[q][a->head[q]] = ticket->slot;
		a->queued[q]++;
		slot->state = RTW88_ASYNC_QUEUED;
	} else {
		release(a, slot);
	}
	return 0;
}

void
rtw88_async_withdraw(struct rtw88_async *a, const struct rtw88_async_epoch *id)
{
	int overflow;

	if (a == NULL || id == NULL || same_epoch(&a->id, id))
		return;
	overflow = exhausted_epoch(id) || id->generation < a->id.generation ||
	    id->epoch < a->id.epoch || id->key_epoch < a->id.key_epoch;
	if (overflow || a->closed) {
		a->closed = 1;
		a->error = EOVERFLOW;
	} else {
		a->id = *id;
	}
	for (unsigned i = 0; i < RTW88_ASYNC_SLOTS; i++) {
		struct rtw88_async_slot *slot = &a->slots[i];

		if (slot->state == RTW88_ASYNC_QUEUED)
			release(a, slot);
		else if (slot->state != RTW88_ASYNC_FREE)
			slot->stale = true;
	}
	memset(a->queued, 0, sizeof(a->queued));
	memset(a->head, 0, sizeof(a->head));
	memset(a->tail, 0, sizeof(a->tail));
}
