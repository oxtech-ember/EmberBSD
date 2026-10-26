/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native FD time and file-flag control. */
#if HAVE_NBTOOL_CONFIG_H
#include "nbtool_config.h"
#endif

#include <sys/stat.h>
#include <assert.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include "compat_stat_time.h"

int
main(void)
{
	struct stat st;
	struct timespec atime, mtime;
	const struct timespec times[2] = {
		{ 1709210096, 123456789 }, { 1709210096, 987654321 }
	};
	int (*set_times)(int, const struct timespec [2]) = futimens;
	int fd;

	fd = open("native-control-file", O_CREAT | O_EXCL | O_RDWR, 0600);
	assert(fd >= 0);
	assert(fstat(fd, &st) == 0);
	assert(fchflags(fd, st.st_flags) == 0);
	assert(set_times(fd, times) == 0);
	assert(fstat(fd, &st) == 0);
	atime = __nbcompat_stat_atime(&st);
	mtime = __nbcompat_stat_mtime(&st);
	assert(atime.tv_sec == times[0].tv_sec);
	assert(atime.tv_nsec == times[0].tv_nsec);
	assert(mtime.tv_sec == times[1].tv_sec);
	assert(mtime.tv_nsec == times[1].tv_nsec);
	assert(close(fd) == 0);
	assert(unlink("native-control-file") == 0);
	return (0);
}
