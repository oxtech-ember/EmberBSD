/* Origin: EmberBSD independent MOXA G2 control/firmware contracts, 2026-10-13. */
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
#include "umoxa_g2_protocol.h"

static int
umoxa_g2_request(const struct umoxa_g2_io *io, int input, uint8_t command,
    uint16_t value, uint16_t index, uint8_t *data, size_t size)
{
	int error;

	if (io == NULL || io->control == NULL)
		return EINVAL;
	error = io->control(io->arg, input, command, value, index, data, size);
	return error < 0 ? EIO : error;
}

int
umoxa_g2_config(const struct umoxa_g2_settings *s, uint8_t line[4],
    uint8_t baud[4])
{
	unsigned i;

	if (s == NULL || line == NULL || baud == NULL || s->baud < 50 ||
	    s->baud > 921600 || s->bits < 5 || s->bits > 8 || s->parity > 2 ||
	    (s->stops != 1 && s->stops != 2) || s->mode > 3 ||
	    s->rtscts > 1 || s->mcr > 3 || (s->mode != 0 && s->rtscts))
		return EINVAL;
	line[0] = s->bits;
	line[1] = s->parity;
	line[2] = s->stops == 1 ? 0 : 2;
	line[3] = 0;
	for (i = 0; i < 4; i++)
		baud[i] = (uint8_t)(s->baud >> (8 * i));
	return 0;
}

static int
umoxa_g2_packet(uint16_t packet)
{

	return packet == 8 || packet == 16 || packet == 32 || packet == 64 ||
	    packet == 512 || packet == 1024;
}

int
umoxa_g2_endpoints(const struct umoxa_g2_endpoint *ep, size_t count,
    struct umoxa_g2_layout *layout)
{
	struct umoxa_g2_layout l = {0};
	size_t i, j;
	uint16_t in_packet = 0, event_packet = 0;

	if (ep == NULL || layout == NULL)
		return EINVAL;
	if (count != 3)
		return EPROTO;
	for (i = 0; i < count; i++) {
		if (ep[i].type != 2 || (ep[i].address & 0x0f) == 0 ||
		    (ep[i].address & 0x70) != 0 || !umoxa_g2_packet(ep[i].packet))
			return EPROTO;
		for (j = 0; j < i; j++)
			if (ep[i].address == ep[j].address)
				return EPROTO;
		if (ep[i].address & 0x80) {
			if (l.data_in == 0) {
				l.data_in = ep[i].address;
				in_packet = ep[i].packet;
			} else if (l.event_in == 0) {
				l.event_in = ep[i].address;
				event_packet = ep[i].packet;
			} else {
				return EPROTO;
			}
		} else {
			if (l.out != 0)
				return EPROTO;
			l.out = ep[i].address;
			l.packet = ep[i].packet;
		}
	}
	if (l.out == 0 || l.event_in == 0 || in_packet != l.packet ||
	    event_packet > l.packet)
		return EPROTO;
	*layout = l;
	return 0;
}

int
umoxa_g2_probe(const struct umoxa_g2_io *io, uint32_t *fw_version, int *remap)
{
	uint8_t v[4] = {0}, r[4] = {0};
	int error;

	if (fw_version == NULL || remap == NULL)
		return EINVAL;
	error = umoxa_g2_request(io, 1, 0x81, 0, 0, v, sizeof(v));
	if (error == 0)
		error = umoxa_g2_request(io, 1, 0x34, 0x20, 0, r, sizeof(r));
	if (error)
		return error;
	if ((r[0] & 3) == 3)
		return EPROTO;
	*fw_version = ((uint32_t)v[0] << 16) | ((uint32_t)v[1] << 8) | v[2];
	*remap = r[0] & 3;
	return 0;
}

int
umoxa_g2_firmware_version(const uint8_t *image, size_t size, uint32_t *fw_version)
{
	uint32_t v;

	if (image == NULL || fw_version == NULL || size < 0x8b ||
	    size > UMOXA_G2_FIRMWARE_MAX)
		return EINVAL;
	v = ((uint32_t)image[0x86] << 16) |
	    ((uint32_t)image[0x88] << 8) | image[0x8a];
	if (v == 0 || v == 0xffffff)
		return EPROTO;
	*fw_version = v;
	return 0;
}

int
umoxa_g2_download(const struct umoxa_g2_io *io, const uint8_t *image,
    size_t size, int *stop_error)
{
	uint8_t block[64];
	uint32_t fw_version;
	size_t pos, n;
	int error;

	if (stop_error == NULL)
		return EINVAL;
	*stop_error = 0;
	if (io == NULL || io->control == NULL)
		return EINVAL;
	error = umoxa_g2_firmware_version(image, size, &fw_version);
	if (error)
		return error;
	error = umoxa_g2_request(io, 0, 0x0c, 0, 0, NULL, 0);
	if (error)
		return error;
	for (pos = 0; pos < size; pos += n) {
		n = size - pos;
		if (n > sizeof(block))
			n = sizeof(block);
		memcpy(block, image + pos, n);
		error = umoxa_g2_request(io, 0, 0x20, 0, 0, block, n);
		if (error)
			return error;
		if (io->delay != NULL)
			io->delay(io->arg, 1);
	}
	*stop_error = umoxa_g2_request(io, 0, 0x0d, 0, 0, NULL, 0);
	/* Even STOP success does not establish the firmware's fw_version or remap. */
	return 0;
}

int
umoxa_g2_quiesce(const struct umoxa_g2_io *io, unsigned port)
{
	const uint8_t commands[] = {7, 8, 9};
	unsigned i;
	int error, first = 0;

	if (port >= 4 || io == NULL || io->control == NULL)
		return EINVAL;
	/* All shutdown operations are attempted; retain the first failure. */
	for (i = 0; i < 3; i++) {
		error = umoxa_g2_request(io, 0, commands[i], i == 2 ? 0x0c : 0,
		    port, NULL, 0);
		if (first == 0)
			first = error;
	}
	return first;
}

int
umoxa_g2_prepare(const struct umoxa_g2_io *io, unsigned port,
    const struct umoxa_g2_settings *s)
{
	uint8_t line[4], baud[4];
	uint16_t rts, dtr;
	int error;

	if (port >= 4)
		return EINVAL;
	error = umoxa_g2_config(s, line, baud);
	if (error)
		return error;
	rts = s->mode != 0 ? 5 : s->rtscts ? 2 : (s->mcr & 2) ? 1 : 5;
	dtr = s->mode == 0 && (s->mcr & 1) ? 1 : 5;
	error = umoxa_g2_request(io, 0, 0x0f, 0, port, NULL, 0);
	if (error == 0)
		error = umoxa_g2_request(io, 0, 0x11, 0, port, NULL, 0);
	if (error == 0)
		error = umoxa_g2_request(io, 0, 0x10, s->mode, port, NULL, 0);
	if (error == 0)
		error = umoxa_g2_request(io, 0, 2, 0, port, line, sizeof(line));
	if (error == 0)
		error = umoxa_g2_request(io, 0, 6, 0, port, NULL, 0);
	if (error == 0)
		error = umoxa_g2_request(io, 0, 4, rts, port, NULL, 0);
	if (error == 0)
		error = umoxa_g2_request(io, 0, 5, dtr, port, NULL, 0);
	if (error == 0)
		error = umoxa_g2_request(io, 0, 1, 0, port, baud, sizeof(baud));
	return error;
}

int
umoxa_g2_enable(const struct umoxa_g2_io *io, unsigned port)
{
	int error;

	if (port >= 4)
		return EINVAL;
	error = umoxa_g2_request(io, 0, 8, 1, port, NULL, 0);
	if (error == 0)
		error = umoxa_g2_request(io, 0, 7, 1, port, NULL, 0);
	return error;
}

int
umoxa_g2_set(const struct umoxa_g2_io *io, unsigned port, int which, int on,
    const struct umoxa_g2_settings *s)
{
	uint8_t line[4], baud[4], command;
	uint16_t value;
	int error;

	if (port >= 4 || (on != 0 && on != 1) || which < 1 || which > 3)
		return EINVAL;
	error = umoxa_g2_config(s, line, baud);
	if (error)
		return error;
	if (which != 3 && s->mode != 0)
		return EOPNOTSUPP;
	if (which == 2 && s->rtscts && !on)
		return EBUSY;
	command = which == 1 ? 5 : which == 2 ? 4 : 11;
	value = which == 3 ? on : on ? 1 : 5;
	if (which == 2 && s->rtscts)
		value = 2;
	return umoxa_g2_request(io, 0, command, value, port, NULL, 0);
}
