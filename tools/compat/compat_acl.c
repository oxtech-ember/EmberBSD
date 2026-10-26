/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/*
 * Public native API composition. No Apple implementation is copied.
 * BSD selection follows bin/ls/print.c; target ls remains unchanged.
 */
#define EMBERBSD_COMPAT_ACL_NATIVE_HEADERS 1
#include "nbtool_config.h"
#undef EMBERBSD_COMPAT_ACL_NATIVE_HEADERS
/* Finite Darwin builds preinclude their configuration before this source. */
#if HAVE_LISTING_ACL_BSD
#include "compat_acl_features.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#if HAVE_LISTING_ACL_BSD || HAVE_LISTING_ACL_DARWIN
#include <sys/acl.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include "compat_acl.h"

#if HAVE_LISTING_ACL_DARWIN
#include "compat_native_acl.h"

static int
presence(const char *path, int *present)
{
	struct stat st;
	acl_t acl = NULL;
	int result, saved;

	if (__nbcompat_darwin_acl_read(-1, path, &st, &acl) == -1)
		return (-1);
	result = __nbcompat_darwin_acl_nonempty(acl, present);
	saved = errno;
	if (acl != NULL)
		(void)acl_free(acl);
	errno = saved;
	return (result);
}
#elif HAVE_LISTING_ACL_BSD
static int
presence(const char *path, int *present)
{
	struct stat st;
	acl_t acl;
	acl_type_t type;
	long supported;
	int trivial, result, saved;

	if (lstat(path, &st) == -1)
		return (-1);
	/* Preserve the canonical UFS device/whiteout exclusion. */
	if (S_ISCHR(st.st_mode) || S_ISBLK(st.st_mode) || S_ISWHT(st.st_mode)) {
		*present = 0;
		return (0);
	}
	type = ACL_TYPE_NFS4;
	errno = 0;
	supported = lpathconf(path, _PC_ACL_NFS4);
	if (supported < 0 && errno != 0 && errno != EINVAL)
		return (-1);
	if (supported <= 0) {
		type = ACL_TYPE_ACCESS;
		errno = 0;
		supported = lpathconf(path, _PC_ACL_EXTENDED);
		if (supported < 0 && errno != 0 && errno != EINVAL)
			return (-1);
	}
	if (supported <= 0) {
		errno = ENOTSUP;
		return (-1);
	}
	acl = acl_get_link_np(path, type);
	if (acl == NULL)
		return (-1);
	result = acl_is_trivial_np(acl, &trivial);
	saved = errno;
	(void)acl_free(acl);
	errno = saved;
	if (result == -1)
		return (-1);
	*present = !trivial;
	return (0);
}
#else
static int
presence(const char *path, int *present)
{

	(void)path;
	(void)present;
	errno = ENOTSUP;
	return (-1);
}
#endif

int
__nbcompat_acl_present_nofollow(const char *path, int *present)
{
	int value, result, saved = errno;

	if (path == NULL || present == NULL) {
		errno = EINVAL;
		return (-1);
	}
	result = presence(path, &value);
	if (result == 0) {
		*present = value;
		errno = saved;
	}
	return (result);
}
