/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/gem/i915_gem_shmem.c (iov_iter_ubuf, ITER_SOURCE,
 * a struct iov_iter on the stack passed to write_iter); the GPL original was not read.
 *
 * <linux/uio.h> for the 6.18 sources: the iterator over one buffer in user memory, as far as
 * the driver asks for it.  Walking an iterator (copy_from_iter) is not here.
 */
#ifndef _DRM618_LINUX_UIO_H_
#define _DRM618_LINUX_UIO_H_

#include <sys/types.h>

#include <linux/types.h>

enum { ITER_DEST = 0, ITER_SOURCE = 1 };

struct iov_iter {
	unsigned int	iter_type;	/* ITER_UBUF */
	bool		data_source;	/* ITER_SOURCE: the data is read from the buffer */
	void __user	*ubuf;
	size_t		count;
};
#define	ITER_UBUF	0

static inline void
iov_iter_ubuf(struct iov_iter *i, unsigned int direction, void __user *buf,
    size_t count)
{

	i->iter_type = ITER_UBUF;
	i->data_source = direction == ITER_SOURCE;
	i->ubuf = buf;
	i->count = count;
}

#endif
