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
static int
presence(const char *path, int *present)
{
	filesec_t security;
	struct stat st;
	acl_t acl = NULL;
	acl_entry_t entry;
	int valid, result = -1, saved;

	security = filesec_init();
	if (security == NULL)
		return (-1);
	if (lstatx_np(path, &st, security) == -1 ||
	    filesec_query_property(security, FILESEC_ACL, &valid) == -1)
		goto out;
	if (!valid) {
		*present = 0;
		result = 0;
		goto out;
	}
	if (filesec_get_property(security, FILESEC_ACL, &acl) == -1)
		goto out;
	if (acl == NULL) {
		errno = EINVAL;
		goto out;
	}
	if (acl_valid(acl) == -1)
		goto out;
	/* For a valid Darwin ACL, FIRST_ENTRY gives EINVAL only if empty. */
	if (acl_get_entry(acl, ACL_FIRST_ENTRY, &entry) == 0)
		*present = 1;
	else if (errno == EINVAL)
		*present = 0;
	else
		goto out;
	result = 0;
out:
	saved = errno;
	if (acl != NULL)
		(void)acl_free(acl);
	filesec_free(security);
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
