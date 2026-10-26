/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native API control, not a copy utility. */
#define EMBERBSD_COMPAT_FILEMETA_NATIVE_HEADERS 1
#include "nbtool_config.h"
#undef EMBERBSD_COMPAT_FILEMETA_NATIVE_HEADERS
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "compat_filemeta.h"
#include "compat_stat_time.h"

int
main(void)
{
	struct stat st;
	struct timespec actual, times[2] = { { 1700000000, 123456789 },
	    { 1700000000, 987654321 } };
	int dir = -1, from = -1, to = -1, saved_stdin = -1, result = 1;

	if (mkdir("cp-control-fixture", 0700) == -1)
		return (1);
	dir = open("cp-control-fixture", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	if (dir == -1)
		goto out;
	saved_stdin = dup(STDIN_FILENO);
	if (saved_stdin == -1 || close(STDIN_FILENO) == -1)
		goto out;
	from = openat(dir, "source", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW,
	    0600);
	if (from != STDIN_FILENO)
		goto out;
	to = openat(dir, "target", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW,
	    0600);
	if (to == -1 || write(from, "x", 1) != 1 ||
	    fchown(from, getuid(), getgid()) == -1 || fchmod(from, 0640) == -1 ||
	    futimens(from, times) == -1 || fstat(from, &st) == -1)
		goto out;
	actual = __nbcompat_stat_atime(&st);
	if (actual.tv_sec != times[0].tv_sec || actual.tv_nsec != times[0].tv_nsec)
		goto out;
	actual = __nbcompat_stat_mtime(&st);
	if (actual.tv_sec != times[1].tv_sec || actual.tv_nsec != times[1].tv_nsec)
		goto out;
	actual = __nbcompat_stat_ctime(&st);
	if (actual.tv_nsec < 0 || actual.tv_nsec >= 1000000000)
		goto out;
	if (__nbcompat_copy_filemeta_fd(from, to,
	    NB_FILEMETA_XATTR | NB_FILEMETA_ACL) == -1 ||
	    fchflags(to, st.st_flags) == -1)
		goto out;
	errno = 0;
	if (__nbcompat_copy_filemeta_fd(-1, to, NB_FILEMETA_XATTR) != -1 ||
	    errno != EBADF)
		goto out;
	if (symlinkat("source", dir, "link") == -1 ||
	    lchmod("cp-control-fixture/link", 0751) == -1 ||
	    lutimens("cp-control-fixture/link", times) == -1)
		goto out;
	result = 0;
out:
	if (from >= 0 && close(from) == -1)
		result = 1;
	if (saved_stdin >= 0) {
		if (dup2(saved_stdin, STDIN_FILENO) == -1)
			result = 1;
		if (close(saved_stdin) == -1)
			result = 1;
	}
	if (to >= 0 && close(to) == -1)
		result = 1;
	if (dir >= 0) {
		if (unlinkat(dir, "link", 0) == -1 && errno != ENOENT)
			result = 1;
		if (unlinkat(dir, "target", 0) == -1 && errno != ENOENT)
			result = 1;
		if (unlinkat(dir, "source", 0) == -1 && errno != ENOENT)
			result = 1;
		if (close(dir) == -1)
			result = 1;
	}
	if (rmdir("cp-control-fixture") == -1)
		result = 1;
	return (result);
}
