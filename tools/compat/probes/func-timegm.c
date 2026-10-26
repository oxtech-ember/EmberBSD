/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native UTC declaration and link probe. */
#include "compat_parsedate_features.h"
#ifdef _NETBSD_SOURCE
#error The UTC declaration adapter must preserve the compatibility namespace.
#endif
#include <time.h>
#include <stddef.h>

static time_t (*volatile utc_convert)(struct tm *) = timegm;

int
main(void)
{
	return utc_convert == NULL;
}
