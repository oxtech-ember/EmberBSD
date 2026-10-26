/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Canonical date parser semantics. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "compat_parsedate.h"

static int
check(const char *text, const int *zone, time_t expected, int error)
{
	const time_t base = 946684800;
	time_t actual;
	int saved;

	/* Successful calls must preserve errno, including a valid time_t -1. */
	errno = EDOM;
	actual = parsedate(text, &base, zone);
	saved = errno;
	if (actual == expected && saved == (error ? error : EDOM))
		return 0;
	(void)fprintf(stderr, "parsedate case failed: %s (errno %d)\n",
	    text, saved);
	return 1;
}

int
main(void)
{
	const char *zones[] = {"UTC0", "EST5"};
	const int utc = 0, west = 300;
	size_t i;

	for (i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
		if (setenv("TZ", zones[i], 1) != 0)
			return 1;
		tzset();
		if (check("2000-01-01T00:00:00Z", NULL, 946684800, 0) ||
		    check("1969-12-31T23:59:59Z", NULL, -1, 0) ||
		    check("@-1", NULL, -1, 0) ||
		    check("January 1, 2000 00:00 UTC", NULL, 946684800, 0) ||
		    check("2000-01-01 00:00:00 +0530", NULL, 946665000, 0) ||
		    check("2000-01-01 00:00:00 -0500", NULL, 946702800, 0) ||
		    check("2000-01-01T00:00:00", &utc, 946684800, 0) ||
		    check("2000-01-01T00:00:00", &west, 946702800, 0) ||
		    check("2000-01-01T00:00:00.125Z", NULL, 946684800, 0) ||
		    check("2 days", NULL, 946857600, 0) ||
		    check("2000-01-31T12:00:00Z 1 month", NULL, 951825600, 0))
			return 2;
		if (check("2000-01-01T00:00:00", NULL,
		    i == 0 ? 946684800 : 946702800, 0))
			return 3;
		if (check("not-a-date", NULL, -1, EINVAL) ||
		    check("25:00", NULL, -1, EINVAL) ||
		    check("2000-01-01T00:00:00Z UTC", NULL, -1, EINVAL) ||
		    check("2000-02-30T00:00:00Z", NULL, -1, EAGAIN) ||
		    check("2147485548-01-01T00:00:00Z", NULL, -1, EOVERFLOW) ||
		    check("9223372036854775807 hours", NULL, -1, EINVAL) ||
		    check("1 second 1 second 1 second 1 second 1 second "
		    "1 second 1 second 1 second 1 second 1 second 1 second "
		    "1 second 1 second 1 second 1 second 1 second 1 second",
		    NULL, -1, EINVAL))
			return 4;
	}
	return 0;
}
