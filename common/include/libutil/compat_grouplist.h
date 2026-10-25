/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Internal native group ABI adapter. */
#ifndef EMBERBSD_COMPAT_GROUPLIST_H
#define EMBERBSD_COMPAT_GROUPLIST_H

#include <sys/types.h>
#include <errno.h>
#include <grp.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

/* This internal header adds no installed library ABI. */
static inline int
compat_getgrouplist(const char *user, gid_t base, gid_t *groups, int *count)
{
#if defined(__APPLE__)
	int *native_groups;
	int capacity, native_base, result, saved_errno, i;
	uintmax_t value;

	/* Darwin's public API uses signed int storage for 32-bit group IDs. */
	_Static_assert(sizeof(gid_t) == sizeof(unsigned int),
	    "unsupported Darwin group ID width");
	value = base;
	if (count == NULL || *count < 0 || (*count > 0 && groups == NULL)) {
		errno = EINVAL;
		return -1;
	}
	if (value > UINT_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	/* Preserve high unsigned IDs without an out-of-range signed cast. */
	native_base = value <= INT_MAX ? (int)value :
	    -1 - (int)(UINT_MAX - value);
	capacity = *count;
	if ((size_t)capacity > SIZE_MAX / sizeof(*native_groups)) {
		errno = EOVERFLOW;
		return -1;
	}
	native_groups = malloc((capacity ? (size_t)capacity : 1) *
	    sizeof(*native_groups));
	if (native_groups == NULL)
		return -1;
	result = getgrouplist(user, native_base, native_groups, count);
	saved_errno = errno;
	if (result >= 0) {
		if (*count < 0 || *count > capacity) {
			result = -1;
			saved_errno = EIO;
		} else {
			for (i = 0; i < *count; i++)
				groups[i] = (gid_t)(unsigned int)native_groups[i];
		}
	} else if (result != -1) {
		result = -1;
		saved_errno = EIO;
	}
	free(native_groups);
	errno = saved_errno;
	return result;
#else
	return getgrouplist(user, base, groups, count);
#endif
}

#endif /* EMBERBSD_COMPAT_GROUPLIST_H */
