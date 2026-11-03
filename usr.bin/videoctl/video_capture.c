/*
 * Copyright (c) 2026 The EmberBSD Project.
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

#include "video_capture.h"

int
video_capture_open_output(const char *path, int *fdp)
{
	int fd;

	if (path == NULL || fdp == NULL)
		return EINVAL;
	*fdp = -1;
	fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd == -1)
		return errno;
	*fdp = fd;
	return 0;
}

int
video_capture_validate_sample(size_t bytesused, size_t capacity, bool error)
{
	if (error || bytesused == 0 || bytesused > capacity)
		return EIO;
	if (capacity == 0)
		return EINVAL;
	return 0;
}

uint64_t
video_capture_checksum(const void *buffer, size_t length)
{
	const uint8_t *data = buffer;
	uint64_t hash = UINT64_C(14695981039346656037);
	size_t i;

	if (data == NULL && length != 0)
		return 0;
	for (i = 0; i < length; i++) {
		hash ^= data[i];
		hash *= UINT64_C(1099511628211);
	}
	return hash;
}
