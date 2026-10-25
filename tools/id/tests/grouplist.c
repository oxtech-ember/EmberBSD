/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Typed public-ABI diagnostic fixture. */
#include <stdlib.h>

#if defined(__APPLE__)
static void *test_realloc(void *, size_t);
#define realloc test_realloc
#endif
#define getgrouplist id_test_getgrouplist
#include <libutil/compat_grouplist.h>
#undef getgrouplist
#if defined(__APPLE__)
#undef realloc
#endif

#include <assert.h>

#if defined(__APPLE__)
typedef int native_gid_t;
#else
typedef gid_t native_gid_t;
#endif

static int calls;
static int fail;
static int required = 2;
#if defined(__APPLE__)
static int prefix;
static int invalid;
static int realloc_calls;
static int fail_realloc;

static void *
test_realloc(void *pointer, size_t bytes)
{
	realloc_calls++;
	if (fail_realloc && realloc_calls == fail_realloc) {
		errno = ENOMEM;
		return NULL;
	}
	return realloc(pointer, bytes);
}
#endif

int
id_test_getgrouplist(const char *user, native_gid_t base, native_gid_t *groups,
    int *count)
{
	int capacity, i;

	(void)user;
	calls++;
	if (fail) {
		*count = 0;
		errno = EIO;
		return -1;
	}
#if defined(__APPLE__)
	assert(base == -2);
#else
	assert(base == (gid_t)-2);
#endif
	capacity = *count;
	*count = required;
#if defined(__APPLE__)
	if (invalid) {
		*count = invalid == 1 || invalid == 5 ? -1 :
		    invalid == 3 ? 0 : capacity + 1;
		return invalid == 4 ? -2 : invalid >= 5 ? -1 : 0;
	}
#endif
	if (capacity < required) {
#if defined(__APPLE__)
		if (prefix) {
			*count = capacity;
			for (i = 0; i < capacity; i++)
				groups[i] = i == 0 ? base : (native_gid_t)(i + 6);
			/* Short-buffer results may leave an unrelated errno. */
			errno = EIO;
		}
#endif
		return -1;
	}
	for (i = 0; i < required; i++)
		groups[i] = i == 0 ? base : (native_gid_t)(i + 6);
	return 0;
}

#if defined(__APPLE__)
static void
darwin_cases(void)
{
	gid_t groups[2], full[515];
	int count, before, mode, i;

	groups[0] = 111;
	groups[1] = 222;
	required = 513;
	prefix = 1;
	count = 1;
	before = calls;
	assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
	assert(count == required && calls - before > 8);
	assert(groups[0] == 111 && groups[1] == 222);
	full[0] = 111;
	full[514] = 222;
	assert(compat_getgrouplist("fixture", (gid_t)-2, full + 1, &count) == 0);
	assert(count == required && full[0] == 111 && full[514] == 222);
	assert(full[1] == (gid_t)-2);
	for (i = 1; i < required; i++)
		assert(full[i + 1] == (gid_t)(i + 6));
	count = 0;
	assert(compat_getgrouplist("fixture", (gid_t)-2, NULL, &count) == -1);
	assert(count == required);

	for (mode = 1; mode <= 5; mode++) {
		invalid = mode;
		count = 2;
		assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
		assert(errno == EIO && count == 2);
		assert(groups[0] == 111 && groups[1] == 222);
	}
	invalid = 6;
	count = 2;
	before = calls;
	assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
	assert(errno == EAGAIN && count == 2);
	assert((unsigned int)(calls - before) == COMPAT_GROUPLIST_RETRIES);
	invalid = 0;

	for (mode = 1; mode <= 3; mode += 2) {
		realloc_calls = 0;
		fail_realloc = mode;
		count = 1;
		assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
		assert(errno == ENOMEM && count == 1);
		assert(groups[0] == 111 && groups[1] == 222);
	}
	fail_realloc = 0;
	count = INT_MAX;
	before = calls;
	assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
	assert(errno == EOVERFLOW && count == INT_MAX && calls == before);
	assert(compat_grouplist_next(COMPAT_GROUPLIST_MAX, COMPAT_GROUPLIST_MAX) == -1);
	assert(errno == EOVERFLOW);
	assert(compat_grouplist_next(1, INT_MAX) == -1 && errno == EOVERFLOW);
	assert(compat_grouplist_next(COMPAT_GROUPLIST_MAX / 2 + 1,
	    COMPAT_GROUPLIST_MAX / 2 + 1) == COMPAT_GROUPLIST_MAX);
	assert(compat_grouplist_next(1, 513) == 513);
	required = 2;
	prefix = 0;
}
#endif

int
main(void)
{
	gid_t groups[2];
	int count;
#if defined(__APPLE__)
	int before;
#endif

	groups[0] = 111;
	groups[1] = 222;
	count = 1;
	assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
	assert(count == 2);
	assert(groups[0] == 111 && groups[1] == 222);
	assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == 0);
	assert(groups[0] == (gid_t)-2 && groups[1] == 7);
#if defined(__APPLE__)
	before = calls;
	count = -1;
	assert(compat_getgrouplist("fixture", 0, groups, &count) == -1);
	assert(errno == EINVAL && calls == before);
	assert(compat_getgrouplist("fixture", 0, groups, NULL) == -1);
	assert(errno == EINVAL && calls == before);
	darwin_cases();
#endif
	fail = 1;
	count = 2;
	assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
	assert(errno == EIO);
	return 0;
}
