/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _FS_LITTLEFS_MOUNT_H_
#define _FS_LITTLEFS_MOUNT_H_

#include <sys/types.h>

#define LITTLEFS_ARGSVERSION	1
#define LITTLEFS_DEFAULT_BLOCK_SIZE	4096

struct littlefs_args {
	const char *fspec;
	uint32_t version;
	uint32_t block_size;	/* zero selects device geometry/default */
};

#endif
