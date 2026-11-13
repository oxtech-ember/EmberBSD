/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 include/linux/eventfd.h
 * (GPL-2.0; no source copied).
 *
 * eventfd is not implemented on this layer.  The lookup fails with
 * -ENOSYS, so DRM syncobj eventfd registration (DRM_IOCTL_SYNCOBJ_EVENTFD)
 * reports an error instead of silently never signalling.
 */

#ifndef _LINUX_EVENTFD_H_
#define _LINUX_EVENTFD_H_

#include <linux/err.h>
#include <linux/errno.h>

struct eventfd_ctx;

static inline struct eventfd_ctx *
eventfd_ctx_fdget(int fd __unused)
{

	return ERR_PTR(-ENOSYS);
}

static inline void
eventfd_ctx_put(struct eventfd_ctx *ctx __unused)
{
}

static inline void
eventfd_signal(struct eventfd_ctx *ctx __unused)
{
}

#endif	/* _LINUX_EVENTFD_H_ */
