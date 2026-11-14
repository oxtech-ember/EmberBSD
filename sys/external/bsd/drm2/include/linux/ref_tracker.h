/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/ref_tracker.h (GPL-2.0-or-later) as
 * it is with reference tracking compiled out: the calls are accepted and
 * track nothing.
 */

#ifndef _LINUX_REF_TRACKER_H_
#define _LINUX_REF_TRACKER_H_

#include <linux/gfp.h>
#include <linux/types.h>

struct ref_tracker;

struct ref_tracker_dir {
	int	rtd_unused;
};

static inline void
ref_tracker_dir_init(struct ref_tracker_dir *dir __unused,
    unsigned int quarantine_count __unused, const char *name __unused)
{
}

static inline void
ref_tracker_dir_exit(struct ref_tracker_dir *dir __unused)
{
}

static inline void
ref_tracker_dir_print_locked(struct ref_tracker_dir *dir __unused,
    unsigned int display_limit __unused)
{
}

static inline void
ref_tracker_dir_print(struct ref_tracker_dir *dir __unused,
    unsigned int display_limit __unused)
{
}

static inline int
ref_tracker_dir_snprint(struct ref_tracker_dir *dir __unused,
    char *buf __unused, size_t size __unused)
{

	return 0;
}

static inline int
ref_tracker_alloc(struct ref_tracker_dir *dir __unused,
    struct ref_tracker **trackerp __unused, gfp_t gfp __unused)
{

	return 0;
}

static inline int
ref_tracker_free(struct ref_tracker_dir *dir __unused,
    struct ref_tracker **trackerp __unused)
{

	return 0;
}

#endif	/* _LINUX_REF_TRACKER_H_ */
