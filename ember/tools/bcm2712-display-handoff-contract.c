/* SPDX-License-Identifier: BSD-2-Clause */
/* Independent byte oracles for the actual one-shot BSD source. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bcm2712_display_handoffvar.h"

static unsigned long checks;
#define CHECK(c) do { checks++; if (!(c)) { \
	fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #c); \
	abort(); } } while (0)

static const uint8_t request_bytes[24] = {
	0x18, 0, 0, 0, 0, 0, 0, 0,
	0x66, 0, 3, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t reply_bytes[24] = {
	0x18, 0, 0, 0, 0, 0, 0, 0x80,
	0x66, 0, 3, 0, 0, 0, 0, 0,
	0, 0, 0, 0x80, 0, 0, 0, 0
};

struct fake {
	struct bcm2712_display_handoff_status *status;
	unsigned int calls;
	uint8_t reply[24];
	int error;
	bool reenter;
};

static int
exchange(void *arg, void *packet, size_t size)
{
	struct fake *f = arg;
	struct bcm2712_display_handoff_status saved;

	f->calls++;
	CHECK(f->calls == 1 && size == 24);
	CHECK(((uintptr_t)packet & 15) == 0);
	CHECK(memcmp(packet, request_bytes, sizeof(request_bytes)) == 0);
	CHECK(f->status->attempted && !f->status->complete && f->status->fault == 0);
	if (f->reenter) {
		memcpy(&saved, f->status, sizeof(saved));
		CHECK(bcm2712_display_notify_done(exchange, f, f->status) == EBUSY);
		CHECK(memcmp(&saved, f->status, sizeof(saved)) == 0 && f->calls == 1);
	}
	memcpy(packet, f->reply, sizeof(f->reply));
	return f->error;
}

static void
setup(struct fake *f, struct bcm2712_display_handoff_status *status)
{

	memset(status, 0, sizeof(*status));
	*f = (struct fake){ .status = status, .reenter = true };
	memcpy(f->reply, reply_bytes, sizeof(f->reply));
}

static void
check_result(struct fake *f, int error)
{
	struct bcm2712_display_handoff_status saved;

	CHECK(bcm2712_display_notify_done(exchange, f, f->status) == error);
	CHECK(f->calls == 1 && f->status->attempted);
	CHECK(f->status->complete == (error == 0) && f->status->fault == error);
	memcpy(&saved, f->status, sizeof(saved));
	CHECK(bcm2712_display_notify_done(exchange, f, f->status) == EBUSY);
	CHECK(f->calls == 1 && memcmp(&saved, f->status, sizeof(saved)) == 0);
}

int
main(void)
{
	struct bcm2712_display_handoff_status status, saved;
	struct fake f;
	static const int errors[] = { EIO, ENXIO, ETIMEDOUT, EBUSY, EOPNOTSUPP, EINVAL };
	unsigned int i, bit;

	setup(&f, &status);
	memcpy(&saved, &status, sizeof(saved));
	CHECK(bcm2712_display_notify_done(NULL, &f, &status) == EINVAL);
	CHECK(memcmp(&saved, &status, sizeof(saved)) == 0 && f.calls == 0);
	CHECK(bcm2712_display_notify_done(exchange, &f, NULL) == EINVAL && f.calls == 0);
	check_result(&f, 0);

	for (i = 0; i < sizeof(reply_bytes); i++) {
		for (bit = 0; bit < 8; bit++) {
			setup(&f, &status);
			f.reply[i] ^= (uint8_t)(1U << bit);
			check_result(&f, i == 19 && bit == 7 ? EOPNOTSUPP : EIO);
		}
	}
	for (i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		setup(&f, &status);
		memset(f.reply, 0xff, sizeof(f.reply));
		f.error = errors[i];
		check_result(&f, errors[i]);
	}
	/* Unsupported must not mask a malformed global/header/end response. */
	for (i = 0; i < sizeof(reply_bytes); i++) {
		if (i >= 16 && i <= 19)
			continue;
		setup(&f, &status);
		f.reply[19] = 0;
		f.reply[i] ^= 1;
		check_result(&f, EIO);
	}
	for (i = 0; i < 3; i++) {
		setup(&f, &status);
		if (i == 0) status.attempted = true;
		if (i == 1) status.complete = true;
		if (i == 2) status.fault = EIO;
		memcpy(&saved, &status, sizeof(saved));
		CHECK(bcm2712_display_notify_done(exchange, &f, &status) == EBUSY);
		CHECK(f.calls == 0 && memcmp(&saved, &status, sizeof(saved)) == 0);
	}
	printf("Display handoff actual-source contract: %lu checks PASS\n", checks);
	return 0;
}
