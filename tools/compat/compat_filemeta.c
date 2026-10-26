/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/* Public native API composition. Target libc and cp/mv remain unchanged. */
#define EMBERBSD_COMPAT_FILEMETA_NATIVE_HEADERS 1
#include "nbtool_config.h"
#undef EMBERBSD_COMPAT_FILEMETA_NATIVE_HEADERS
#include "compat_filemeta_features.h"
#include "compat_filemeta.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if HAVE_FILEMETA_XATTR_DARWIN
#include <sys/xattr.h>
#elif HAVE_FILEMETA_XATTR_BSD_INT || HAVE_FILEMETA_XATTR_BSD_SSIZE
#include <sys/extattr.h>
#endif
#if HAVE_FILEMETA_ACL_DARWIN
#include "compat_native_acl.h"
#elif HAVE_FILEMETA_ACL_BSD
#include <sys/acl.h>
#endif

struct endpoint {
	int fd;
	const char *path;
	struct stat st;
};

static int
same_object(const struct stat *a, const struct stat *b)
{

	if (a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
	    (a->st_mode & S_IFMT) == (b->st_mode & S_IFMT))
		return (0);
	errno = EAGAIN;
	return (-1);
}

static int
representation(const struct stat *st)
{

	(void)st;
#ifdef UF_COMPRESSED
	if ((st->st_flags & UF_COMPRESSED) != 0) {
		errno = ENOTSUP;
		return (-1);
	}
#endif
#ifdef SF_DATALESS
	if ((st->st_flags & SF_DATALESS) != 0) {
		errno = ENOTSUP;
		return (-1);
	}
#endif
	return (0);
}

static int
fd_endpoint(struct endpoint *ep, int fd)
{

	ep->fd = fd;
	ep->path = NULL;
	if (fstat(fd, &ep->st) == -1)
		return (-1);
	if (!S_ISREG(ep->st.st_mode) && !S_ISDIR(ep->st.st_mode)) {
		errno = ENOTSUP;
		return (-1);
	}
	return (representation(&ep->st));
}

static int
path_endpoint(struct endpoint *ep, const char *path)
{
#if HAVE_FILEMETA_OPEN
	struct stat opened;
	int flags, saved;
#endif

	ep->fd = -1;
	ep->path = path;
	if (lstat(path, &ep->st) == -1 || representation(&ep->st) == -1)
		return (-1);
	if (S_ISLNK(ep->st.st_mode))
		return (0);
#if HAVE_FILEMETA_OPEN
	flags = O_RDONLY | O_NOFOLLOW | O_CLOEXEC;
	if (S_ISDIR(ep->st.st_mode))
		flags |= O_DIRECTORY;
#if HAVE_FILEMETA_REGULAR_OPEN
	else if (S_ISREG(ep->st.st_mode))
		flags |= O_REGULAR;
#endif
	else {
		errno = ENOTSUP;
		return (-1);
	}
	ep->fd = open(path, flags);
	if (ep->fd == -1)
		return (-1);
	if (fstat(ep->fd, &opened) == -1 ||
	    same_object(&ep->st, &opened) == -1) {
		saved = errno;
		(void)close(ep->fd);
		ep->fd = -1;
		errno = saved;
		return (-1);
	}
	ep->path = NULL;
	return (0);
#else
	errno = ENOTSUP;
	return (-1);
#endif
}

#if HAVE_FILEMETA_XATTR_DARWIN || HAVE_FILEMETA_XATTR_BSD_INT || \
    HAVE_FILEMETA_XATTR_BSD_SSIZE
static ssize_t
attribute_list(const struct endpoint *ep, int space, void *buffer, size_t size)
{

#if HAVE_FILEMETA_XATTR_DARWIN
	(void)space;
	return (ep->fd >= 0 ? flistxattr(ep->fd, buffer, size, 0) :
	    listxattr(ep->path, buffer, size, XATTR_NOFOLLOW));
#elif HAVE_FILEMETA_XATTR_BSD_INT || HAVE_FILEMETA_XATTR_BSD_SSIZE
	return (ep->fd >= 0 ? extattr_list_fd(ep->fd, space, buffer, size) :
	    extattr_list_link(ep->path, space, buffer, size));
#else
	(void)ep;
	(void)space;
	(void)buffer;
	(void)size;
	errno = ENOTSUP;
	return (-1);
#endif
}

static ssize_t
attribute_get(const struct endpoint *ep, int space, const char *name,
    void *buffer, size_t size)
{

#if HAVE_FILEMETA_XATTR_DARWIN
	(void)space;
	return (ep->fd >= 0 ? fgetxattr(ep->fd, name, buffer, size, 0, 0) :
	    getxattr(ep->path, name, buffer, size, 0, XATTR_NOFOLLOW));
#elif HAVE_FILEMETA_XATTR_BSD_INT || HAVE_FILEMETA_XATTR_BSD_SSIZE
	return (ep->fd >= 0 ?
	    extattr_get_fd(ep->fd, space, name, buffer, size) :
	    extattr_get_link(ep->path, space, name, buffer, size));
#else
	(void)ep;
	(void)space;
	(void)name;
	(void)buffer;
	(void)size;
	errno = ENOTSUP;
	return (-1);
#endif
}

static int
attribute_set(const struct endpoint *ep, int space, const char *name,
    const void *buffer, size_t size)
{
#if !HAVE_FILEMETA_XATTR_DARWIN && \
    (HAVE_FILEMETA_XATTR_BSD_INT || HAVE_FILEMETA_XATTR_BSD_SSIZE)
	ssize_t written;
#endif

#if HAVE_FILEMETA_XATTR_DARWIN
	(void)space;
	return (ep->fd >= 0 ? fsetxattr(ep->fd, name, buffer, size, 0, 0) :
	    setxattr(ep->path, name, buffer, size, 0, XATTR_NOFOLLOW));
#elif HAVE_FILEMETA_XATTR_BSD_INT || HAVE_FILEMETA_XATTR_BSD_SSIZE
	written = ep->fd >= 0 ?
	    extattr_set_fd(ep->fd, space, name, buffer, size) :
	    extattr_set_link(ep->path, space, name, buffer, size);
	if (written == -1)
		return (-1);
	if ((size_t)written != size) {
		errno = EIO;
		return (-1);
	}
	return (0);
#else
	(void)ep;
	(void)space;
	(void)name;
	(void)buffer;
	(void)size;
	errno = ENOTSUP;
	return (-1);
#endif
}

static int
read_attribute(const struct endpoint *ep, int space, const char *name,
    void **out, size_t *length)
{
	void *buffer = NULL, *replacement;
	ssize_t size, got;
	int attempt, saved;

	for (attempt = 0; attempt < 3; attempt++) {
		size = name == NULL ? attribute_list(ep, space, NULL, 0) :
		    attribute_get(ep, space, name, NULL, 0);
		if (size == -1)
			break;
		if ((uintmax_t)size > SIZE_MAX ||
		    (uintmax_t)size > PTRDIFF_MAX) {
			errno = EOVERFLOW;
			break;
		}
		replacement = realloc(buffer, size == 0 ? 1 : (size_t)size);
		if (replacement == NULL)
			break;
		buffer = replacement;
		got = name == NULL ?
		    attribute_list(ep, space, buffer, (size_t)size) :
		    attribute_get(ep, space, name, buffer, (size_t)size);
		if (got == size) {
			*out = buffer;
			*length = (size_t)size;
			return (0);
		}
		if (got == -1 && errno == ERANGE)
			continue;
		if (got >= 0)
			errno = EAGAIN;
		break;
	}
	saved = errno;
	free(buffer);
	errno = saved;
	return (-1);
}

static int
copy_namespace(const struct endpoint *from, const struct endpoint *to,
    int space)
{
	void *list = NULL, *value = NULL;
	char name[256];
#if HAVE_FILEMETA_XATTR_DARWIN
	char *next;
#endif
	const char *item;
	size_t length, offset, namesize, size;
	int result = -1, saved;

	if (read_attribute(from, space, NULL, &list, &length) == -1)
		return (-1);
	for (offset = 0; offset < length;) {
		item = (char *)list + offset;
#if HAVE_FILEMETA_XATTR_DARWIN
		next = memchr(item, '\0', length - offset);
		if (next == NULL || next == item) {
			errno = EIO;
			goto out;
		}
		namesize = (size_t)(next - item);
		offset += namesize + 1;
#else
		namesize = (unsigned char)*item++;
		if (namesize == 0 || namesize >= sizeof(name) ||
		    namesize > length - offset - 1 ||
		    memchr(item, '\0', namesize) != NULL) {
			errno = EIO;
			goto out;
		}
		offset += namesize + 1;
#endif
		if (namesize >= sizeof(name)) {
			errno = EOVERFLOW;
			goto out;
		}
		memcpy(name, item, namesize);
		name[namesize] = '\0';
#if HAVE_FILEMETA_XATTR_DARWIN
		if (strcmp(name, "com.apple.decmpfs") == 0) {
			errno = ENOTSUP;
			goto out;
		}
#endif
		if (read_attribute(from, space, name, &value, &size) == -1 ||
		    attribute_set(to, space, name, value, size) == -1)
			goto out;
		free(value);
		value = NULL;
	}
	result = 0;
out:
	saved = errno;
	free(value);
	free(list);
	errno = saved;
	return (result);
}
#endif /* native xattr backend */

static int
copy_attributes(const struct endpoint *from, const struct endpoint *to)
{

#if HAVE_FILEMETA_XATTR_DARWIN
	return (copy_namespace(from, to, 0));
#elif HAVE_FILEMETA_XATTR_BSD_INT || HAVE_FILEMETA_XATTR_BSD_SSIZE
	if (copy_namespace(from, to, EXTATTR_NAMESPACE_USER) == -1)
		return (-1);
	/* Match the canonical extattr namespace privilege selection. */
	if (getuid() == 0)
		return (copy_namespace(from, to, EXTATTR_NAMESPACE_SYSTEM));
	return (0);
#else
	(void)from;
	(void)to;
	errno = ENOTSUP;
	return (-1);
#endif
}

#if HAVE_FILEMETA_ACL_DARWIN
static int
copy_acl(const struct endpoint *from, const struct endpoint *to)
{
	acl_t acl = NULL;
	struct stat observed;
	int result = -1, present, saved;

	if (__nbcompat_darwin_acl_read(from->fd, from->path,
	    &observed, &acl) == -1)
		return (-1);
	if (same_object(&from->st, &observed) == -1 ||
	    __nbcompat_darwin_acl_nonempty(acl, &present) == -1)
		goto out;
	if (!present) {
		result = 0;
		goto out;
	}
	/* Darwin's path setter follows non-links. Use only the FD setter. */
	if (to->fd < 0) {
		errno = ENOTSUP;
		goto out;
	}
	result = acl_set_fd_np(to->fd, acl, ACL_TYPE_EXTENDED);
out:
	saved = errno;
	if (acl != NULL)
		(void)acl_free(acl);
	errno = saved;
	return (result);
}
#elif HAVE_FILEMETA_ACL_BSD
static int
copy_acl_type(const struct endpoint *from, const struct endpoint *to,
    acl_type_t type, int default_acl)
{
	acl_t acl;
	acl_entry_t entry;
	int result = -1, trivial, entries, saved;

	acl = from->fd >= 0 ? acl_get_fd_np(from->fd, type) :
	    acl_get_link_np(from->path, type);
	if (acl == NULL)
		return (-1);
	if (default_acl) {
		entries = acl_get_entry(acl, ACL_FIRST_ENTRY, &entry);
		if (entries == -1)
			goto out;
		trivial = entries == 0;
	} else if (acl_is_trivial_np(acl, &trivial) == -1)
		goto out;
	if (trivial)
		result = 0;
	else
		result = to->fd >= 0 ? acl_set_fd_np(to->fd, acl, type) :
		    acl_set_link_np(to->path, type, acl);
out:
	saved = errno;
	(void)acl_free(acl);
	errno = saved;
	return (result);
}

static int
copy_acl(const struct endpoint *from, const struct endpoint *to)
{
	long support;
	acl_type_t type;
	int query;

	type = ACL_TYPE_NFS4;
	query = _PC_ACL_NFS4;
	for (;;) {
		errno = 0;
		support = from->fd >= 0 ? fpathconf(from->fd, query) :
		    lpathconf(from->path, query);
		if (support == -1 && errno != 0 && errno != EINVAL)
			return (-1);
		if (support > 0)
			break;
		if (query == _PC_ACL_EXTENDED) {
			errno = ENOTSUP;
			return (-1);
		}
		query = _PC_ACL_EXTENDED;
		type = ACL_TYPE_ACCESS;
	}
	if (copy_acl_type(from, to, type, 0) == -1)
		return (-1);
	if (type == ACL_TYPE_ACCESS && S_ISDIR(from->st.st_mode))
		return (copy_acl_type(from, to, ACL_TYPE_DEFAULT, 1));
	return (0);
}
#else
static int
copy_acl(const struct endpoint *from, const struct endpoint *to)
{

	(void)from;
	(void)to;
	errno = ENOTSUP;
	return (-1);
}
#endif

static int
copy_metadata(const struct endpoint *from, const struct endpoint *to,
    unsigned int parts)
{
	struct stat current;

	if ((parts & NB_FILEMETA_XATTR) != 0 && copy_attributes(from, to) == -1)
		return (-1);
	if ((parts & NB_FILEMETA_ACL) != 0 && copy_acl(from, to) == -1)
		return (-1);
	if (from->path != NULL && (lstat(from->path, &current) == -1 ||
	    same_object(&from->st, &current) == -1))
		return (-1);
	if (to->path != NULL && (lstat(to->path, &current) == -1 ||
	    same_object(&to->st, &current) == -1))
		return (-1);
	return (0);
}

static int
valid_parts(unsigned int parts)
{

	if (parts == 0 ||
	    (parts & ~(NB_FILEMETA_XATTR | NB_FILEMETA_ACL)) != 0) {
		errno = EINVAL;
		return (-1);
	}
	return (0);
}

int
__nbcompat_copy_filemeta_fd(int source_fd, int destination_fd,
    unsigned int parts)
{
	struct endpoint from, to;
	int result, saved = errno;

	if (valid_parts(parts) == -1 || fd_endpoint(&from, source_fd) == -1 ||
	    fd_endpoint(&to, destination_fd) == -1)
		return (-1);
	result = copy_metadata(&from, &to, parts);
	if (result == 0)
		errno = saved;
	return (result);
}

int
__nbcompat_copy_filemeta_nofollow(const char *source, const char *destination,
    unsigned int parts)
{
	struct endpoint from, to;
	int result = -1, saved, original = errno;

	if (valid_parts(parts) == -1)
		return (-1);
	if (source == NULL || destination == NULL) {
		errno = EINVAL;
		return (-1);
	}
	if (path_endpoint(&from, source) == -1)
		return (-1);
	to.fd = -1;
	if (path_endpoint(&to, destination) == 0)
		result = copy_metadata(&from, &to, parts);
	saved = errno;
	if (to.fd >= 0 && close(to.fd) == -1 && result == 0) {
		result = -1;
		saved = errno;
	}
	if (from.fd >= 0 && close(from.fd) == -1 && result == 0) {
		result = -1;
		saved = errno;
	}
	errno = result == 0 ? original : saved;
	return (result);
}
