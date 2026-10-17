/* Origin: EmberBSD UPort 1150 wire and lifecycle implementation, 2026-10-13. */
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
#include "umoxa1150_protocol.h"

int
umoxa1150_config(const struct umoxa1150_settings *s, uint8_t out[10])
{
	uint8_t b[10];
	uint16_t divisor, flags;

	if (s == NULL || out == NULL || s->baud < 50 || s->baud > 921600 ||
	    s->bits < 5 || s->bits > 8 || s->parity > 2 ||
	    (s->stops != 1 && s->stops != 2) || s->mode > 2 ||
	    s->rtscts > 1 || s->ixon > 1 || s->ixoff > 1 || s->send_break > 1)
		return EINVAL;
	divisor = 923077 / s->baud;
	flags = 0x6000;
	if (s->parity != 0)
		flags |= 0x0008;
	if (s->rtscts)
		flags |= 0x0021;
	if (s->ixon)
		flags |= 0x0040;
	if (s->ixoff)
		flags |= 0x0100;
	if (s->send_break)
		flags |= 0x8000;
	b[0] = divisor >> 8; b[1] = divisor;
	b[2] = flags >> 8; b[3] = flags;
	b[4] = s->bits - 5; b[5] = s->parity;
	b[6] = s->stops == 2 ? 2 : 0;
	b[7] = s->xon; b[8] = s->xoff; b[9] = s->mode;
	memcpy(out, b, sizeof(b));
	return 0;
}

int
umoxa1150_firmware(const uint8_t *src, size_t size, uint8_t *out, size_t cap)
{
	size_t i;
	uint8_t sum = 0;

	if (src == NULL || out == NULL || size < 3 || size > UMOXA1150_IMAGE_SIZE)
		return EINVAL;
	if (cap < UMOXA1150_IMAGE_SIZE)
		return EMSGSIZE;
	/* The first three bytes are transport metadata, not executable payload. */
	memmove(out + 3, src + 3, size - 3);
	memset(out + size, 0xff, UMOXA1150_IMAGE_SIZE - size);
	for (i = 3; i < UMOXA1150_IMAGE_SIZE; i++)
		sum += out[i];
	out[0] = 0x9c; out[1] = 0x3f; out[2] = sum;
	return 0;
}

int
umoxa1150_endpoints(const struct umoxa1150_endpoint *ep, size_t count, int cold,
    struct umoxa1150_layout *out)
{
	struct umoxa1150_layout l = {0};
	size_t i, j;

	if (ep == NULL || out == NULL || (cold != 0 && cold != 1))
		return EINVAL;
	if (count != (cold ? 1 : 3))
		return EPROTO;
	for (i = 0; i < count; i++) {
		if ((ep[i].address & 0x0f) == 0 ||
		    (ep[i].address & 0x70) != 0 || ep[i].packet == 0)
			return EPROTO;
		for (j = 0; j < i; j++)
			if (ep[j].address == ep[i].address)
				return EPROTO;
		if (ep[i].type == 2 && !(ep[i].address & 0x80) && l.out == 0)
			l.out = ep[i].address;
		else if (!cold && ep[i].type == 2 && (ep[i].address & 0x80) && l.in == 0)
			l.in = ep[i].address;
		else if (!cold && ep[i].type == 3 && (ep[i].address & 0x80) &&
		    l.interrupt == 0 && ep[i].packet >= 2 && ep[i].packet <= 64) {
			l.interrupt = ep[i].address;
			l.interrupt_size = ep[i].packet;
		} else
			return EPROTO;
	}
	if (l.out == 0 || (!cold && (l.in == 0 || l.interrupt == 0)))
		return EPROTO;
	*out = l;
	return 0;
}

int
umoxa1150_event(const uint8_t *data, size_t size, struct umoxa1150_event *out)
{
	struct umoxa1150_event e = {0};

	if (data == NULL || out == NULL)
		return EINVAL;
	if (size != 2)
		return EPROTO;
	if (data[0] == 0xff)
		return EIO;
	if (data[0] != 0x33 && data[0] != 0x34)
		return EPROTO;
	e.kind = data[0] & 0x0f;
	if (e.kind == 4)
		e.msr = data[1];
	else
		e.lsr = (data[1] & 0x0f) << 1;
	*out = e;
	return 0;
}

int
umoxa1150_apply(const struct umoxa1150_io *io,
    const struct umoxa1150_settings *s, uint8_t mcr)
{
	uint8_t config[10], control[9] = {0x30, 1, 1, 0, 0, 0xff, 0xa4, 0x34, 0};
	int error;

	if (io == NULL || io->control == NULL || (mcr & ~0x30) != 0)
		return EINVAL;
	error = umoxa1150_config(s, config);
	if (error)
		return error;
	error = io->control(io->arg, 5, 0, 3, config, sizeof(config));
	if (error)
		return error;
	/* SET_CONFIG asserts modem lines; restore the requested shadow explicitly. */
	control[8] = mcr;
	return io->control(io->arg, 0x80, 0, 5, control, sizeof(control));
}

int
umoxa1150_stop(const struct umoxa1150_io *io)
{
	int first, error;

	if (io == NULL || io->control == NULL)
		return EINVAL;
	first = io->control(io->arg, 9, 0, 3, NULL, 0);
	error = io->control(io->arg, 7, 0, 3, NULL, 0);
	return first ? first : error;
}

int
umoxa1150_start(const struct umoxa1150_io *io,
    const struct umoxa1150_settings *s, uint8_t mcr)
{
	uint8_t probe[10];
	int error;

	if (io == NULL || io->control == NULL || (mcr & ~0x30) != 0 ||
	    umoxa1150_config(s, probe) != 0)
		return EINVAL;
	error = umoxa1150_apply(io, s, mcr);
	if (error == 0)
		error = io->control(io->arg, 6, 0x89, 3, NULL, 0);
	if (error == 0)
		error = io->control(io->arg, 8, 0, 3, NULL, 0);
	if (error == 0)
		error = io->control(io->arg, 11, 0x80, 3, NULL, 0);
	if (error == 0)
		error = io->control(io->arg, 11, 0, 3, NULL, 0);
	if (error)
		(void)umoxa1150_stop(io);
	return error;
}

int
umoxa1150_upload(const struct umoxa1150_io *io, const uint8_t *image, size_t size)
{
	size_t pos, n, actual;
	uint8_t sum = 0;
	int error;

	if (io == NULL || io->control == NULL || io->bulk == NULL || image == NULL)
		return EINVAL;
	if (size != UMOXA1150_IMAGE_SIZE || image[0] != 0x9c || image[1] != 0x3f)
		return EPROTO;
	for (pos = 3; pos < size; pos++)
		sum += image[pos];
	if (sum != image[2])
		return EPROTO;
	for (pos = 0; pos < size; pos += n) {
		n = size - pos;
		if (n > 64)
			n = 64;
		actual = 0;
		error = io->bulk(io->arg, image + pos, n, &actual);
		if (error)
			return error;
		if (actual != n)
			return EIO;
	}
	/* The parent resets only after closing the cold pipe, then verifies warm. */
	return 0;
}
