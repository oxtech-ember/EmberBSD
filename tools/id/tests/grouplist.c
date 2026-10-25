/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Typed public-ABI diagnostic fixture. */
#define getgrouplist id_test_getgrouplist
#include <libutil/compat_grouplist.h>
#undef getgrouplist

#include <assert.h>

#if defined(__APPLE__)
typedef int native_gid_t;
#else
typedef gid_t native_gid_t;
#endif

static int calls;
static int fail;

int
id_test_getgrouplist(const char *user, native_gid_t base, native_gid_t *groups,
    int *count)
{
	int capacity;

	(void)user;
	calls++;
	if (fail) {
		errno = EIO;
		return -1;
	}
#if defined(__APPLE__)
	assert(base == -2);
#else
	assert(base == (gid_t)-2);
#endif
	capacity = *count;
	*count = 2;
	if (capacity < 2)
		return -1;
	groups[0] = base;
	groups[1] = 7;
	return 0;
}

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
#endif
	fail = 1;
	count = 2;
	assert(compat_getgrouplist("fixture", (gid_t)-2, groups, &count) == -1);
	assert(errno == EIO);
	return 0;
}
