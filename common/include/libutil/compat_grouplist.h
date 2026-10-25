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

#if defined(__APPLE__)
#define COMPAT_GROUPLIST_RETRIES (sizeof(int) * CHAR_BIT)
/* Libinfo passes capacity + 1 to its lookup backend. */
#define COMPAT_GROUPLIST_MAX (INT_MAX - 1)

static inline int
compat_grouplist_next(int capacity, int reported)
{

	if (capacity <= 0 || capacity > COMPAT_GROUPLIST_MAX ||
	    reported < capacity) {
		errno = EINVAL;
		return -1;
	}
	if (reported > COMPAT_GROUPLIST_MAX ||
	    capacity == COMPAT_GROUPLIST_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	if (reported > capacity)
		return reported;
	return capacity > COMPAT_GROUPLIST_MAX / 2 ?
	    COMPAT_GROUPLIST_MAX : capacity * 2;
}
#endif

/* This internal header adds no installed library ABI. */
static inline int
compat_getgrouplist(const char *user, gid_t base, gid_t *groups, int *count)
{
#if defined(__APPLE__)
	int *native_groups, *next;
	int capacity, native_base, native_capacity, native_count;
	int result, status, saved_errno, i;
	uintmax_t value;
	unsigned int attempt;

	/* Darwin's public API uses signed int storage for 32-bit group IDs. */
	_Static_assert(sizeof(gid_t) == sizeof(unsigned int),
	    "unsupported Darwin group ID width");
	value = base;
	if (user == NULL || count == NULL || *count < 0 ||
	    (*count > 0 && groups == NULL)) {
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
	if (capacity > COMPAT_GROUPLIST_MAX) {
		errno = EOVERFLOW;
		return -1;
	}
	native_groups = NULL;
	native_capacity = capacity ? capacity : 1;
	result = -1;
	saved_errno = EAGAIN;
	for (attempt = 0; attempt < COMPAT_GROUPLIST_RETRIES; attempt++) {
		if ((size_t)native_capacity > SIZE_MAX / sizeof(*native_groups)) {
			saved_errno = EOVERFLOW;
			break;
		}
		next = realloc(native_groups,
		    (size_t)native_capacity * sizeof(*native_groups));
		if (next == NULL) {
			saved_errno = errno ? errno : ENOMEM;
			break;
		}
		native_groups = next;
		native_count = native_capacity;
		status = getgrouplist(user, native_base, native_groups,
		    &native_count);
		saved_errno = errno;
		if (status >= 0) {
			if (native_count <= 0 || native_count > native_capacity) {
				saved_errno = EIO;
				break;
			}
			/* Publish only a complete count, never a truncated prefix. */
			*count = native_count;
			if (native_count > capacity) {
				saved_errno = ERANGE;
				break;
			}
			for (i = 0; i < native_count; i++)
				groups[i] = (gid_t)(unsigned int)native_groups[i];
			result = status;
			break;
		}
		if (status != -1 || native_count < 0) {
			saved_errno = EIO;
			break;
		}
		if (native_count < native_capacity) {
			if (saved_errno == 0)
				saved_errno = EIO;
			break;
		}
		/* A full prefix means a short buffer, irrespective of errno. */
		native_capacity = compat_grouplist_next(native_capacity,
		    native_count);
		if (native_capacity < 0) {
			saved_errno = errno;
			break;
		}
		saved_errno = EAGAIN;
	}
	free(native_groups);
	errno = saved_errno;
	return result;
#else
	return getgrouplist(user, base, groups, count);
#endif
}

#endif /* EMBERBSD_COMPAT_GROUPLIST_H */
