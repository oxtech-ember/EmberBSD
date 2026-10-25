/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native UTC declaration and link probe. */
#include "compat_parsedate_features.h"
#include <time.h>
#include <stddef.h>

static time_t (*volatile utc_convert)(timezone_t, struct tm *) = mktime_z;

int
main(void)
{
	return utc_convert == NULL;
}
