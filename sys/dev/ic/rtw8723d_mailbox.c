/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native RTL8723D integration, AI-assisted. */
/* 11N register mailbox order/addresses from pinned dual-BSD Linux rtw88
 * fw.c/fw.h/reg.h adc218676eef25575469234709c2d87185ca223a.
 * Native reservation, generation and uncertainty handling, AI-assisted. */
#ifdef _KERNEL
#include <sys/systm.h>
#else
#include <string.h>
#endif
#include <dev/ic/rtw8723d_mailbox.h>
#include <dev/ic/rtw88_io.h>

int
rtw8723d_mailbox_reset(struct rtw8723d_mailbox *box, uint32_t generation)
{
	if (box == NULL || generation == 0)
		return EINVAL;
	if (box->generation != 0 && generation <= box->generation)
		return ESTALE;
	memset(box, 0, sizeof(*box));
	box->generation = generation;
	return 0;
}

static int
rtw8723d_mailbox_check(struct rtw88_io *io,
    const struct rtw8723d_mailbox *box, uint32_t generation,
    int (*check)(void *), void *cookie)
{
	if (io->error != 0)
		return io->error;
	if (box == NULL || generation == 0)
		return io->error = EINVAL;
	if (box->generation != generation)
		return io->error = ESTALE;
	if (box->next > 3)
		return io->error = EPROTO;
	if (box->uncertain || box->error != 0)
		return io->error = box->error != 0 ? box->error : EIO;
	if (check != NULL)
		io->error = check(cookie);
	return io->error;
}

int
rtw8723d_mailbox_begin(struct rtw88_io *io, struct rtw8723d_mailbox *box,
    uint32_t generation, unsigned count, int *ready,
    int (*check)(void *), void *cookie)
{
	struct rtw88_io validation;
	unsigned mask = 0, i;
	uint8_t occupied;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (ready == NULL || count > 3)
		return io->error = EINVAL;
	*ready = 0;
	if (rtw8723d_mailbox_check(io, box, generation, check, cookie) != 0)
		return io->error;
	if (box->active)
		return EBUSY;
	if (count == 0) {
		*ready = 1;
		return 0;
	}
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	for (i = 0; i < count; i++)
		mask |= 1U << ((box->next + i) & 3);
	occupied = rtw88_io_rd(io, 0x1cc, 1);
	if (rtw8723d_mailbox_check(io, box, generation, check, cookie) != 0)
		return io->error;
	if ((occupied & mask) != 0)
		return 0;
	box->first = box->next;
	box->total = count;
	box->sent = 0;
	box->active = true;
	*ready = 1;
	return 0;
}

int
rtw8723d_mailbox_end(struct rtw8723d_mailbox *box,
    uint32_t generation, int error)
{
	if (box == NULL)
		return error != 0 ? error : EINVAL;
	if (error == 0 && box->error != 0)
		error = box->error;
	if (error == 0 && box->generation != generation)
		error = ESTALE;
	if (box->active && error == 0 && box->sent != box->total)
		error = EPROTO;
	if (box->active && box->sent != 0 && error != 0) {
		box->uncertain = true;
		if (box->error == 0)
			box->error = error;
	}
	box->active = false;
	box->first = box->total = box->sent = 0;
	return error;
}

int
rtw8723d_mailbox_send(struct rtw88_io *io, struct rtw8723d_mailbox *box,
    uint32_t generation, uint8_t id, const void *payload, size_t size,
    int (*check)(void *), void *cookie)
{
	struct rtw88_io validation;
	uint8_t bytes[8] = { 0 }, occupied;
	uint32_t low, high;
	unsigned i, next, polls;

	if (io == NULL)
		return EINVAL;
	if (io->error != 0)
		return io->error;
	if (size > 7 || (size != 0 && payload == NULL))
		return io->error = size > 7 ? EMSGSIZE : EINVAL;
	bytes[0] = id;
	if (size != 0)
		memcpy(bytes + 1, payload, size);
	if (rtw8723d_mailbox_check(io, box, generation, check, cookie) != 0)
		return io->error;
	if (box->active && (box->sent >= box->total ||
	    ((box->first + box->sent) & 3) != box->next))
		return io->error = EPROTO;
	rtw88_io_init(&validation, io->ops, io->cookie);
	if (validation.error != 0)
		return io->error = validation.error;
	next = box->next;
	for (polls = 0; polls < 100; polls++) {
		if (rtw8723d_mailbox_check(io, box, generation, check, cookie) != 0)
			return io->error;
		occupied = rtw88_io_rd(io, 0x1cc, 1);
		if (rtw8723d_mailbox_check(io, box, generation, check, cookie) != 0)
			return io->error;
		if ((occupied & (1U << next)) == 0)
			break;
		if (box->active)
			return EBUSY;
		if (polls != 99)
			rtw88_io_delay(io, 100);
	}
	if (polls == 100)
		return io->error = ETIMEDOUT;
	low = high = 0;
	for (i = 0; i < 4; i++) {
		low |= (uint32_t)bytes[i] << (i * 8);
		high |= (uint32_t)bytes[i + 4] << (i * 8);
	}
	box->uncertain = true; /* Publish quarantine before possible side effect. */
	rtw88_io_wr(io, 0x1f0 + next * 4, 4, high);
	/* Check external ownership without rejecting our own in-flight marker. */
	if (io->error == 0 && check != NULL)
		io->error = check(cookie);
	if (io->error == 0 && box->generation != generation)
		io->error = ESTALE;
	if (io->error == 0)
		rtw88_io_wr(io, 0x1d0 + next * 4, 4, low);
	if (io->error == 0 && check != NULL)
		io->error = check(cookie);
	if (io->error == 0 && box->generation != generation)
		io->error = ESTALE;
	if (io->error != 0) {
		if (box->error == 0)
			box->error = io->error;
		return io->error;
	}
	box->next = (next + 1) & 3;
	if (box->active)
		box->sent++;
	box->uncertain = false;
	return 0;
}
