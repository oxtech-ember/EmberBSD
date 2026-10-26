/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>

static int (*volatile query)(char *, size_t, int64_t, const char *, int, int) =
    humanize_number;

int
main(void)
{
	(void)(HN_DECIMAL | HN_NOSPACE | HN_B | HN_DIVISOR_1000 |
	    HN_GETSCALE | HN_AUTOSCALE);
	return (query == NULL);
}
