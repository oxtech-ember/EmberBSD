/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD. Shadow of <linux/file.h>: descriptors of Linux files (drm618_file.h). */
#ifndef _DRM618_LINUX_FILE_H_
#define _DRM618_LINUX_FILE_H_
#include <drm618_file.h>
#include <linux/cleanup.h>

/* A descriptor looked up: fdget takes a reference on the file, fdput drops it. */
struct fd {
	struct DRM618_FILE_TAG	*file;
};

#define	fd_file(f)	((f).file)
#define	fd_empty(f)	((f).file == NULL)

static inline struct fd
fdget(unsigned int number)
{
	struct fd fd = { .file = fget(number) };

	return fd;
}

static inline void
fdput(struct fd fd)
{

	if (fd.file != NULL)
		fput(fd.file);
}

DEFINE_CLASS(fd, struct fd, fdput(_T), fdget(fd), int fd)
#endif
