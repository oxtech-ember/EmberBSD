/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Shared host and target stat access. */
#ifndef EMBERBSD_COMPAT_STAT_TIME_H
#define EMBERBSD_COMPAT_STAT_TIME_H

#include <sys/stat.h>
#include <time.h>

/* Include the selected host configuration before this header. */

static __inline struct timespec
__nbcompat_stat_atime(const struct stat *st)
{
#if HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_ATIM
	return (st->st_atim);
#elif defined(__NetBSD__) || \
    (HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec value;

	value.tv_sec = st->st_atime;
	value.tv_nsec = st->st_atimensec;
	return (value);
#else
	return (st->st_atimespec);
#endif
}

static __inline struct timespec
__nbcompat_stat_mtime(const struct stat *st)
{
#if HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_ATIM
	return (st->st_mtim);
#elif defined(__NetBSD__) || \
    (HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec value;

	value.tv_sec = st->st_mtime;
	value.tv_nsec = st->st_mtimensec;
	return (value);
#else
	return (st->st_mtimespec);
#endif
}

static __inline struct timespec
__nbcompat_stat_ctime(const struct stat *st)
{
#if HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_ATIM
	return (st->st_ctim);
#elif defined(__NetBSD__) || \
    (HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_MTIMENSEC)
	struct timespec value;

	value.tv_sec = st->st_ctime;
	value.tv_nsec = st->st_ctimensec;
	return (value);
#else
	return (st->st_ctimespec);
#endif
}

#endif /* EMBERBSD_COMPAT_STAT_TIME_H */
