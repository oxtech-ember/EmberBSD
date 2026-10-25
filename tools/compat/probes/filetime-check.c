/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Canonical file-time component semantics. */
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include "compat_filetime.h"

int
main(void)
{
	const struct timespec first[2] = {{946684800, 0}, {946684801, 0}};
	const struct timespec second[2] = {{946684802, 0}, {946684803, 0}};
	struct stat sb;
	int fd;

	fd = open("filetime-file", O_CREAT | O_EXCL | O_RDWR, 0600);
	if (fd == -1 || close(fd) == -1)
		return 1;
	if (utimens("filetime-file", first) == -1 ||
	    stat("filetime-file", &sb) == -1 ||
	    sb.st_atime != first[0].tv_sec || sb.st_mtime != first[1].tv_sec)
		return 2;
	if (symlink("filetime-file", "filetime-link") == -1 ||
	    lutimens("filetime-link", second) == -1 ||
	    lstat("filetime-link", &sb) == -1 ||
	    sb.st_atime != second[0].tv_sec || sb.st_mtime != second[1].tv_sec)
		return 3;
	if (stat("filetime-file", &sb) == -1 ||
	    sb.st_atime != first[0].tv_sec || sb.st_mtime != first[1].tv_sec)
		return 4;
	errno = 0;
	if (utimens("filetime-missing", first) != -1 || errno != ENOENT)
		return 5;
	if (unlink("filetime-link") == -1 || unlink("filetime-file") == -1)
		return 6;
	return 0;
}
