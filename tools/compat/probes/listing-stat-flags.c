/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <sys/stat.h>
int
main(void)
{
	struct stat st = {0};
	st.st_flags = UF_APPEND | UF_IMMUTABLE | UF_NODUMP | UF_OPAQUE |
	    SF_APPEND | SF_ARCHIVED | SF_IMMUTABLE;
	return (st.st_flags == 0);
}
