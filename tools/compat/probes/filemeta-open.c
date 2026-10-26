/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include "compat_filemeta_features.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stddef.h>
#include <unistd.h>

static int (*volatile open_file)(const char *, int, ...) = open;
static int (*volatile close_file)(int) = close;
static int (*volatile fd_stat)(int, struct stat *) = fstat;
static int (*volatile link_stat)(const char *, struct stat *) = lstat;

int
main(void)
{

	(void)(O_NOFOLLOW | O_DIRECTORY | O_CLOEXEC);
	return (open_file == NULL || close_file == NULL || fd_stat == NULL ||
	    link_stat == NULL);
}
