/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted; quiet parser and legacy parity checks. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dev/videomode/edidvar.h>
#include <dev/videomode/vesagtf.h>

static unsigned checks, prints;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s\n", __LINE__, #c); exit(1); } checks++; } while (0)
#ifndef __unused
#define __unused __attribute__((unused))
#endif
static int count_print(const char *fmt, ...) { (void)fmt; prints++; return 0; }
#define _KERNEL 1
#define DIAGNOSTIC 1
#define aprint_debug count_print
#define printf count_print
#include "videomode.c"
#include "vesagtf.c"
#include "edid.c"
#undef printf
#undef _KERNEL

static void
checksum(uint8_t data[128])
{
	unsigned sum = 0;

	for (unsigned n = 0; n < 127; n++)
		sum += data[n];
	data[127] = (uint8_t)(0 - sum);
}

static void
base(uint8_t data[128])
{
	static const uint8_t signature[8] = { 0, 255, 255, 255, 255, 255, 255, 0 };

	memset(data, 0, 128);
	memcpy(data, signature, sizeof(signature));
	data[18] = 1; data[19] = 3; data[20] = 0x80;
	/* Established 640x480 and standard 1280x1024 at 60 Hz. */
	data[35] = 0x20; data[38] = 129; data[39] = 0x80;
	checksum(data);
}

static void
compare(const struct edid_info *a, const struct edid_info *b)
{
	CHECK(a->edid_nmodes == b->edid_nmodes);
	CHECK(a->edid_range.er_max_clock == b->edid_range.er_max_clock);
	CHECK(a->edid_video_input == b->edid_video_input);
	CHECK(a->edid_ext_block_count == b->edid_ext_block_count);
	CHECK((a->edid_preferred_mode == NULL) == (b->edid_preferred_mode == NULL));
	if (a->edid_preferred_mode != NULL)
		CHECK(a->edid_preferred_mode - a->edid_modes ==
		    b->edid_preferred_mode - b->edid_modes);
	for (int n = 0; n < a->edid_nmodes; n++) {
		const struct videomode *x = &a->edid_modes[n], *y = &b->edid_modes[n];
		CHECK(x->dot_clock == y->dot_clock && x->flags == y->flags);
		CHECK(x->hdisplay == y->hdisplay && x->htotal == y->htotal);
		CHECK(x->hsync_start == y->hsync_start && x->hsync_end == y->hsync_end);
		CHECK(x->vdisplay == y->vdisplay && x->vtotal == y->vtotal);
		CHECK(x->vsync_start == y->vsync_start && x->vsync_end == y->vsync_end);
	}
}

int
main(void)
{
	uint8_t data[128], saved[128];
	struct edid_info legacy, quiet;

	for (unsigned profile = 0; profile < 3; profile++) {
		base(data);
		if (profile == 1) {
			/* 1280x1024, 108 MHz, separate positive syncs. */
			const uint8_t dtd[18] = { 0x30, 0x2a, 0, 0x98, 0x51, 0, 0x2a, 0x40,
			    0x30, 0x70, 0x13, 0, 0, 0, 0, 0, 0, 0x1e };
			memcpy(data + 54, dtd, sizeof(dtd));
		}
		if (profile == 2) data[39] = 0xc1; /* Existing GTF fallback. */
		checksum(data); memcpy(saved, data, sizeof(saved));
		memset(&legacy, 0, sizeof(legacy)); memset(&quiet, 0, sizeof(quiet));
		prints = 0; CHECK(edid_parse(data, &legacy) == 0 && prints == 1);
		prints = 0; CHECK(edid_parse_quiet(data, &quiet) == 0 && prints == 0);
		CHECK(memcmp(data, saved, sizeof(data)) == 0);
		CHECK(quiet.edid_nmodes >= 2);
		compare(&legacy, &quiet);
		if (profile == 1) CHECK(quiet.edid_preferred_mode != NULL);
	}
	/* Exercise the DIAGNOSTIC-only missing established-mode message. */
	const char *name = _edid_modes[13];
	_edid_modes[13] = "fixture-missing-mode";
	base(data); memset(&legacy, 0, sizeof(legacy)); memset(&quiet, 0, sizeof(quiet));
	prints = 0; CHECK(edid_parse(data, &legacy) == 0 && prints == 2);
	prints = 0; CHECK(edid_parse_quiet(data, &quiet) == 0 && prints == 0);
	compare(&legacy, &quiet); _edid_modes[13] = name;
	/* Invalid signature/checksum retain the caller's output bytes. */
	for (unsigned failure = 0; failure < 2; failure++) {
		base(data); data[failure == 0 ? 0 : 127] ^= 1;
		memset(&quiet, 0x5a, sizeof(quiet)); legacy = quiet;
		prints = 0; CHECK(edid_parse_quiet(data, &quiet) == -1 && prints == 0);
		CHECK(memcmp(&quiet, &legacy, sizeof(quiet)) == 0);
	}
	printf("%u quiet EDID parser checks passed\n", checks);
	return 0;
}
