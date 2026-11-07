/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _FS_LITTLEFS_KERNEL_H_
#define _FS_LITTLEFS_KERNEL_H_

#include <sys/param.h>
#include <sys/systm.h>
#include <fs/littlefs/littlefs_namespace.h>

/* Mount and vnode objects own all upstream cache buffers. */
#define LFS_NO_MALLOC
#define LFS_NO_DEBUG
#define LFS_NO_WARN
#define LFS_NO_ERROR
#define LFS_ASSERT(test)	KASSERT(test)

#endif /* _FS_LITTLEFS_KERNEL_H_ */
