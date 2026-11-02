/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../video_capture.h"

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

static void
test_sample_bounds(void)
{
	CHECK(video_capture_validate_sample(1, 1, false) == 0);
	CHECK(video_capture_validate_sample(640 * 480 * 2,
	    640 * 480 * 2, false) == 0);
	CHECK(video_capture_validate_sample(0, 16, false) == EIO);
	CHECK(video_capture_validate_sample(17, 16, false) == EIO);
	CHECK(video_capture_validate_sample(16, 16, true) == EIO);
	CHECK(video_capture_validate_sample(0, 0, false) == EIO);
}

int
main(void)
{
	test_sample_bounds();
	CHECK(video_capture_checksum("foobar", 6) ==
	    UINT64_C(0x85944171f73967e8));
	CHECK(video_capture_checksum(NULL, 0) == UINT64_C(14695981039346656037));
	return 0;
}
