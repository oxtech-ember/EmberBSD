/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Required native UTC backend semantics. */
#include "compat_parsedate_features.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>

static time_t (*volatile utc_convert)(struct tm *) = timegm;

int
main(void)
{
	struct tm utc = {.tm_year = 100, .tm_mon = 0, .tm_mday = 1};
	struct tm local = utc;
	struct tm before = {.tm_year = 69, .tm_mon = 11, .tm_mday = 31,
	    .tm_hour = 23, .tm_min = 59, .tm_sec = 59};

	/* A fixed POSIX zone needs no external zoneinfo file. */
	if (setenv("TZ", "EST5", 1) != 0)
		return 1;
	tzset();
	if (mktime(&local) != (time_t)946702800 ||
	    utc_convert(&utc) != (time_t)946684800)
		return 2;
	errno = 0;
	if (utc_convert(&before) != (time_t)-1 || errno != 0)
		return 3;
	return 0;
}
