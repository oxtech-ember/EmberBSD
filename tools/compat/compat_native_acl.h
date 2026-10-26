/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#ifndef EMBERBSD_COMPAT_NATIVE_ACL_H
#define EMBERBSD_COMPAT_NATIVE_ACL_H

/* Shared public Darwin API composition; no native ACL layout is exposed. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <errno.h>
#include <fcntl.h>

static __inline int
__nbcompat_darwin_acl_read(int fd, const char *path, struct stat *st, acl_t *out)
{
	filesec_t security;
	acl_t acl = NULL;
	int result = -1, valid, saved;

	security = filesec_init();
	if (security == NULL)
		return (-1);
	if ((fd >= 0 ? fstatx_np(fd, st, security) :
	    lstatx_np(path, st, security)) == -1 ||
	    filesec_query_property(security, FILESEC_ACL, &valid) == -1)
		goto out;
	if (valid) {
		if (filesec_get_property(security, FILESEC_ACL, &acl) == -1)
			goto out;
		if (acl == NULL) {
			errno = EINVAL;
			goto out;
		}
		if (acl_valid(acl) == -1)
			goto out;
	}
	*out = acl;
	acl = NULL;
	result = 0;
out:
	saved = errno;
	if (acl != NULL)
		(void)acl_free(acl);
	filesec_free(security);
	errno = saved;
	return (result);
}

static __inline int
__nbcompat_darwin_acl_nonempty(acl_t acl, int *present)
{
	acl_entry_t entry;
	int result, saved = errno;

	if (acl == NULL) {
		*present = 0;
		return (0);
	}
	/* A validated Darwin ACL reports an empty first entry with EINVAL. */
	result = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
	if (result == 0)
		*present = 1;
	else if (result == -1 && errno == EINVAL)
		*present = 0;
	else {
		if (result != -1)
			errno = EIO;
		return (-1);
	}
	errno = saved;
	return (0);
}

#endif /* EMBERBSD_COMPAT_NATIVE_ACL_H */
