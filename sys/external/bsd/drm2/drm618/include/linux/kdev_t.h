/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD. Shadow of <linux/kdev_t.h>: old_encode_dev. */
#ifndef _DRM618_LINUX_KDEV_T_H_
#define _DRM618_LINUX_KDEV_T_H_
#include_next <linux/kdev_t.h>
/*
 * The 16 bit device number of old user space: major << 8 | minor.  Linux
 * numbers (MKDEV) are the argument.
 */
static inline unsigned
old_encode_dev(unsigned dev)
{

	return (MAJOR(dev) << 8) | MINOR(dev);
}
#endif
