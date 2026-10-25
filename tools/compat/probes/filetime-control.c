/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Required native file-time interface. */
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>

static int (*volatile update)(int, const char *, const struct timespec *, int) =
    utimensat;

int
main(void)
{
	(void)AT_FDCWD;
	(void)AT_SYMLINK_NOFOLLOW;
	return update == NULL;
}
