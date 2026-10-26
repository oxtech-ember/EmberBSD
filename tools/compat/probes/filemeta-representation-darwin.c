/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <sys/stat.h>

int
main(void)
{
	struct stat st = {0};

	return ((st.st_flags & (UF_COMPRESSED | SF_DATALESS)) != 0);
}
