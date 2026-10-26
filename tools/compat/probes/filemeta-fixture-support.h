/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Shared native diagnostic fixtures. */
#ifndef EMBERBSD_FILEMETA_FIXTURE_SUPPORT_H
#define EMBERBSD_FILEMETA_FIXTURE_SUPPORT_H
#include <sys/types.h>
#include <sys/acl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <errno.h>
#include <membership.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>

static const unsigned char binary[] = { 0, 'x', 255, '\n' };
static const char shared[] = "org.emberbsd.filemeta.shared";
static const char empty[] = "org.emberbsd.filemeta.empty";
static const char retained[] = "org.emberbsd.filemeta.retained";

static inline int
value_equals(int fd, const char *name, const void *value, size_t size)
{
	unsigned char buffer[16];
	ssize_t got;

	got = fgetxattr(fd, name, buffer, sizeof(buffer), 0, 0);
	return (got >= 0 && (size_t)got == size &&
	    memcmp(buffer, value, size) == 0);
}

static inline acl_t
make_acl(int inherit)
{
	acl_t acl;
	acl_entry_t entry;
	acl_permset_t permissions;
	acl_flagset_t flags;
	uuid_t owner;
	int index, saved;

	acl = acl_init(2);
	if (acl == NULL)
		return (NULL);
	if (mbr_uid_to_uuid(getuid(), owner) != 0)
		goto fail;
	for (index = 0; index < 2; index++) {
		if (acl_create_entry(&acl, &entry) == -1 ||
		    acl_set_tag_type(entry, ACL_EXTENDED_ALLOW) == -1 ||
		    acl_set_qualifier(entry, owner) == -1 ||
		    acl_get_permset(entry, &permissions) == -1 ||
		    acl_clear_perms(permissions) == -1 ||
		    acl_add_perm(permissions, index == 0 ? ACL_READ_DATA :
		    ACL_READ_ATTRIBUTES) == -1 ||
		    acl_set_permset(entry, permissions) == -1)
			goto fail;
		if (inherit && (acl_get_flagset_np(entry, &flags) == -1 ||
		    acl_add_flag_np(flags, ACL_ENTRY_FILE_INHERIT) == -1 ||
		    acl_add_flag_np(flags, ACL_ENTRY_DIRECTORY_INHERIT) == -1 ||
		    acl_set_flagset_np(entry, flags) == -1))
			goto fail;
	}
	if (acl_valid(acl) == -1)
		goto fail;
	return (acl);
fail:
	saved = errno;
	(void)acl_free(acl);
	errno = saved;
	return (NULL);
}

static inline int
acl_values_equal(acl_t observed, acl_t expected)
{
	char *a = NULL, *b = NULL;
	ssize_t alen, blen;
	int equal = 0, saved;

	a = acl_to_text(expected, &alen);
	b = acl_to_text(observed, &blen);
	if (a != NULL && b != NULL && alen == blen && alen >= 0)
		equal = memcmp(a, b, (size_t)alen) == 0;
	saved = errno;
	if (a != NULL)
		(void)acl_free(a);
	if (b != NULL)
		(void)acl_free(b);
	errno = saved;
	return (equal);
}

static inline int
acl_equals(int fd, acl_t expected)
{
	acl_t observed;
	int equal, saved;

	observed = acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
	if (observed == NULL)
		return (0);
	equal = acl_values_equal(observed, expected);
	saved = errno;
	(void)acl_free(observed);
	errno = saved;
	return (equal);
}

static inline int
acl_path_equals(const char *name, acl_t expected)
{
	acl_t observed;
	int equal, saved;

	observed = acl_get_link_np(name, ACL_TYPE_EXTENDED);
	if (observed == NULL)
		return (0);
	equal = acl_values_equal(observed, expected);
	saved = errno;
	(void)acl_free(observed);
	errno = saved;
	return (equal);
}

static inline int
acl_empty_fd(int fd, acl_t empty_acl)
{
	filesec_t security;
	struct stat st;
	acl_t observed = NULL;
	int present, equal = 0, saved;

	security = filesec_init();
	if (security == NULL)
		return (0);
	if (fstatx_np(fd, &st, security) == -1 ||
	    filesec_query_property(security, FILESEC_ACL, &present) == -1)
		goto out;
	if (!present) {
		equal = 1;
		goto out;
	}
	if (filesec_get_property(security, FILESEC_ACL, &observed) == 0 &&
	    observed != NULL)
		equal = acl_values_equal(observed, empty_acl);
out:
	saved = errno;
	if (observed != NULL)
		(void)acl_free(observed);
	filesec_free(security);
	errno = saved;
	return (equal);
}

static inline int
value_path_equals(const char *path, const char *name, const void *value,
    size_t size)
{
	unsigned char buffer[16];
	ssize_t got;

	got = getxattr(path, name, buffer, sizeof(buffer), 0, XATTR_NOFOLLOW);
	return (got >= 0 && (size_t)got == size &&
	    memcmp(buffer, value, size) == 0);
}

#endif
