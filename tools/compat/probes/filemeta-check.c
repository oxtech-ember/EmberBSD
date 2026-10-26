/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/* Independent native getters check the common API's real effects. */
#include <sys/types.h>
#include <sys/acl.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <membership.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include "compat_filemeta.h"

#include "filemeta-fixture-support.h"

int
main(void)
{
	acl_t clear = NULL, source_acl = NULL, directory_acl = NULL;
	acl_t source_observed = NULL, directory_observed = NULL;
	struct stat before, after;
	struct statfs filesystem;
	int src = -1, dst = -1, srcdir = -1, dstdir = -1, input = -1;
	int srcmade = 0, dstmade = 0, srcdirmade = 0, dstdirmade = 0;
	int srclink = 0, dstlink = 0, special = 0, rebound = 0;
	int result = 1, saved, closed, stage = 1;

#define CHECK(expression) do { if (!(expression)) goto out; } while (0)
	src = open("filemeta-source", O_CREAT | O_EXCL | O_RDWR, 0600);
	CHECK(src >= 0);
	srcmade = 1;
	dst = open("filemeta-destination", O_CREAT | O_EXCL | O_RDWR, 0600);
	CHECK(dst >= 0);
	dstmade = 1;
	CHECK(fstatfs(src, &filesystem) == 0);
	clear = acl_init(0);
	CHECK(clear != NULL &&
	    acl_set_fd_np(src, clear, ACL_TYPE_EXTENDED) == 0);
	CHECK(fsetxattr(dst, retained, "keep", 4, 0, 0) == 0);
	CHECK(__nbcompat_copy_filemeta_fd(src, dst, NB_FILEMETA_XATTR) == 0 &&
	    value_equals(dst, retained, "keep", 4));

	stage = 2;
	CHECK(fsetxattr(src, shared, binary, sizeof(binary), 0, 0) == 0 &&
	    fsetxattr(src, empty, "", 0, 0, 0) == 0 &&
	    fsetxattr(dst, shared, "old", 3, 0, 0) == 0);
	errno = EDOM;
	CHECK(__nbcompat_copy_filemeta_fd(src, dst, NB_FILEMETA_XATTR) == 0 &&
	    errno == EDOM &&
	    value_equals(dst, shared, binary, sizeof(binary)) &&
	    value_equals(dst, empty, "", 0) &&
	    value_equals(dst, retained, "keep", 4));
	CHECK(fcntl(src, F_GETFD) != -1 && fcntl(dst, F_GETFD) != -1);
	input = dup(STDIN_FILENO);
	CHECK(input >= 0 && dup2(src, STDIN_FILENO) == STDIN_FILENO);
	rebound = 1;
	CHECK(__nbcompat_copy_filemeta_fd(STDIN_FILENO, dst,
	    NB_FILEMETA_XATTR) == 0);
	CHECK(dup2(input, STDIN_FILENO) == STDIN_FILENO);
	rebound = 0;
	closed = close(input);
	input = -1;
	CHECK(closed == 0);

	stage = 3;
	source_acl = make_acl(0);
	CHECK(source_acl != NULL &&
	    acl_set_fd_np(src, source_acl, ACL_TYPE_EXTENDED) == 0);
	CHECK(acl_equals(src, source_acl));
	source_observed = acl_get_fd_np(src, ACL_TYPE_EXTENDED);
	CHECK(source_observed != NULL);
	CHECK(__nbcompat_copy_filemeta_fd(src, dst,
	    NB_FILEMETA_XATTR | NB_FILEMETA_ACL) == 0);
	CHECK(acl_equals(dst, source_observed));
	CHECK(acl_set_fd_np(src, clear, ACL_TYPE_EXTENDED) == 0 &&
	    __nbcompat_copy_filemeta_fd(src, dst, NB_FILEMETA_ACL) == 0 &&
	    acl_equals(dst, source_observed));

	stage = 4;
	CHECK(mkdir("filemeta-source-directory", 0700) == 0);
	srcdirmade = 1;
	CHECK(mkdir("filemeta-destination-directory", 0700) == 0);
	dstdirmade = 1;
	srcdir = open("filemeta-source-directory",
	    O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	dstdir = open("filemeta-destination-directory",
	    O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	CHECK(srcdir >= 0 && dstdir >= 0);
	directory_acl = make_acl(1);
	CHECK(directory_acl != NULL &&
	    acl_set_fd_np(srcdir, directory_acl, ACL_TYPE_EXTENDED) == 0);
	CHECK(acl_equals(srcdir, directory_acl));
	directory_observed = acl_get_fd_np(srcdir, ACL_TYPE_EXTENDED);
	CHECK(directory_observed != NULL &&
	    fsetxattr(srcdir, shared, binary, sizeof(binary), 0, 0) == 0);
	CHECK(__nbcompat_copy_filemeta_nofollow("filemeta-source-directory",
	    "filemeta-destination-directory",
	    NB_FILEMETA_XATTR | NB_FILEMETA_ACL) == 0);
	CHECK(acl_equals(dstdir, directory_observed) &&
	    value_equals(dstdir, shared, binary, sizeof(binary)));

	stage = 5;
	CHECK(symlink("filemeta-source", "filemeta-source-link") == 0);
	srclink = 1;
	CHECK(symlink("filemeta-destination",
	    "filemeta-destination-link") == 0);
	dstlink = 1;
	CHECK(fstat(dst, &before) == 0 &&
	    __nbcompat_copy_filemeta_nofollow("filemeta-source-link",
	    "filemeta-destination-link", NB_FILEMETA_ACL) == 0);
	CHECK(__nbcompat_copy_filemeta_nofollow("filemeta-source-link",
	    "filemeta-destination-link", NB_FILEMETA_XATTR) == 0);
	errno = 0;
	CHECK(__nbcompat_copy_filemeta_nofollow("filemeta-source-directory",
	    "filemeta-destination-link", NB_FILEMETA_ACL) == -1 &&
	    errno == ENOTSUP);
	CHECK(fstat(dst, &after) == 0 && before.st_dev == after.st_dev &&
	    before.st_ino == after.st_ino && before.st_mode == after.st_mode &&
	    before.st_size == after.st_size &&
	    acl_equals(dst, source_observed) &&
	    value_equals(dst, shared, binary, sizeof(binary)));

	stage = 6;
	errno = 0;
	CHECK(__nbcompat_copy_filemeta_fd(src, dst, 0) == -1 &&
	    errno == EINVAL);
	errno = 0;
	CHECK(__nbcompat_copy_filemeta_fd(src, dst, 0x80U) == -1 &&
	    errno == EINVAL);
	errno = 0;
	CHECK(__nbcompat_copy_filemeta_fd(-1, dst, NB_FILEMETA_XATTR) == -1 &&
	    errno == EBADF);
	errno = 0;
	CHECK(__nbcompat_copy_filemeta_nofollow("filemeta-missing",
	    "filemeta-destination-directory", NB_FILEMETA_ACL) == -1 &&
	    errno == ENOENT);
	CHECK(mkfifo("filemeta-special", 0600) == 0);
	special = 1;
	errno = 0;
	CHECK(__nbcompat_copy_filemeta_nofollow("filemeta-special",
	    "filemeta-destination-directory", NB_FILEMETA_XATTR) == -1 &&
	    errno == ENOTSUP);
	CHECK(value_equals(dst, retained, "keep", 4));
	(void)printf("filemeta: FD zero, files, directories, "
	    "ACL order and inheritance, xattr overlay and empty values, "
	    "no-follow links, errors; filesystem=%.*s\n",
	    (int)sizeof(filesystem.f_fstypename), filesystem.f_fstypename);
	result = 0;
out:
	saved = errno;
	if (rebound && dup2(input, STDIN_FILENO) != STDIN_FILENO)
		result = 1;
	if (input >= 0 && close(input) != 0)
		result = 1;
	if (source_observed != NULL)
		(void)acl_free(source_observed);
	if (directory_observed != NULL)
		(void)acl_free(directory_observed);
	if (source_acl != NULL)
		(void)acl_free(source_acl);
	if (directory_acl != NULL)
		(void)acl_free(directory_acl);
	if (clear != NULL)
		(void)acl_free(clear);
	if (src >= 0 && close(src) != 0)
		result = 1;
	if (dst >= 0 && close(dst) != 0)
		result = 1;
	if (srcdir >= 0 && close(srcdir) != 0)
		result = 1;
	if (dstdir >= 0 && close(dstdir) != 0)
		result = 1;
	if (srclink && unlink("filemeta-source-link") != 0)
		result = 1;
	if (dstlink && unlink("filemeta-destination-link") != 0)
		result = 1;
	if (special && unlink("filemeta-special") != 0)
		result = 1;
	if (srcmade && unlink("filemeta-source") != 0)
		result = 1;
	if (dstmade && unlink("filemeta-destination") != 0)
		result = 1;
	if (srcdirmade && rmdir("filemeta-source-directory") != 0)
		result = 1;
	if (dstdirmade && rmdir("filemeta-destination-directory") != 0)
		result = 1;
	if (result != 0)
		(void)fprintf(stderr, "filemeta case %d failed (errno %d)\n",
		    stage, saved);
	return (result);
}
