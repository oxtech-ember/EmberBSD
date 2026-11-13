/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native.  Interface of Linux 6.18 include/linux/kmsg_dump.h
 * (none in the first lines; GPL-2.0 file; no source copied).
 *
 * Panic log dumper registration is not supported: the registration calls
 * fail with -EINVAL, as Linux does without CONFIG_PRINTK.  Needed because
 * drm_plane.h embeds a struct kmsg_dumper.
 */

#ifndef _LINUX_KMSG_DUMP_H_
#define _LINUX_KMSG_DUMP_H_

#include <linux/errno.h>
#include <linux/list.h>
#include <linux/types.h>

enum kmsg_dump_reason {
	KMSG_DUMP_UNDEF,
	KMSG_DUMP_PANIC,
	KMSG_DUMP_OOPS,
	KMSG_DUMP_EMERG,
	KMSG_DUMP_SHUTDOWN,
	KMSG_DUMP_MAX
};

struct kmsg_dump_detail {
	enum kmsg_dump_reason reason;
	const char *description;
};

struct kmsg_dumper {
	struct list_head list;
	void (*dump)(struct kmsg_dumper *dumper, struct kmsg_dump_detail *detail);
	enum kmsg_dump_reason max_reason;
	bool registered;
};

static inline int
kmsg_dump_register(struct kmsg_dumper *dumper __unused)
{

	return -EINVAL;
}

static inline int
kmsg_dump_unregister(struct kmsg_dumper *dumper __unused)
{

	return -EINVAL;
}

#endif	/* _LINUX_KMSG_DUMP_H_ */
