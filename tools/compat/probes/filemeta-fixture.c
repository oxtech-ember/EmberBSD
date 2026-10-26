/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Fixed native metadata test operations. */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "filemeta-fixture-support.h"

static const char *const cases[] = {
	"file-replace", "file-overlay", "empty-acl-overlay",
	"directory-inherit"
};

static int
prepare(unsigned int selected)
{
	acl_t clear = NULL, wanted = NULL;
	int src = -1, dst = -1, result = 1, saved;
	int directory = selected == 3;

	/* O_EXCL does not open an existing special file or follow a link. */
	if (directory) {
		if (mkdir("src", 0700) == -1 || mkdir("dst", 0700) == -1)
			goto out;
		src = open("src", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
		dst = open("dst", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
	} else {
		src = open("src", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
		dst = open("dst", O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
	}
	if (src < 0 || dst < 0)
		goto out;
	if (!directory && (write(src, "source\n", 7) != 7 ||
	    write(dst, "old\n", 4) != 4))
		goto out;
	clear = acl_init(0);
	wanted = make_acl(directory);
	if (clear == NULL || wanted == NULL ||
	    acl_set_fd_np(src, selected == 2 ? clear : wanted,
	    ACL_TYPE_EXTENDED) == -1 ||
	    acl_set_fd_np(dst, selected == 2 ? wanted : clear,
	    ACL_TYPE_EXTENDED) == -1)
		goto out;
	if (fsetxattr(src, shared, binary, sizeof(binary), 0, 0) == -1 ||
	    fsetxattr(src, empty, "", 0, 0, 0) == -1 ||
	    fsetxattr(dst, shared, "old", 3, 0, 0) == -1 ||
	    (selected != 0 && fsetxattr(dst, retained, "keep", 4, 0, 0) == -1))
		goto out;
	if (!value_equals(src, shared, binary, sizeof(binary)) ||
	    !value_equals(src, empty, "", 0) ||
	    !value_equals(dst, shared, "old", 3) ||
	    (selected != 0 && !value_equals(dst, retained, "keep", 4)) ||
	    !(selected == 2 ? acl_empty_fd(src, clear) : acl_equals(src, wanted)) ||
	    !(selected == 2 ? acl_equals(dst, wanted) : acl_empty_fd(dst, clear)))
		goto out;
	result = 0;
out:
	saved = errno;
	if (src >= 0 && close(src) != 0) {
		if (result == 0)
			saved = errno;
		result = 1;
	}
	if (dst >= 0 && close(dst) != 0) {
		if (result == 0)
			saved = errno;
		result = 1;
	}
	if (clear != NULL)
		(void)acl_free(clear);
	if (wanted != NULL)
		(void)acl_free(wanted);
	errno = saved;
	return (result);
}

/* Darwin can represent an empty ACL by an absent filesec property. */
static int
acl_empty_path(const char *path, acl_t empty_acl)
{
	filesec_t security;
	struct stat st;
	acl_t observed = NULL;
	int present, equal = 0, saved;

	security = filesec_init();
	if (security == NULL)
		return (0);
	if (lstatx_np(path, &st, security) == -1 ||
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

static int
observe(unsigned int selected, int move)
{
	struct stat before[2], after;
	const char *const paths[] = { "src", "dst" };
	unsigned char value;
	acl_t wanted;
	unsigned int index;
	int result = 1, saved;

	if (move && selected == 3)
		return (2);
	for (index = 0; index < 2; index++) {
		if (lstat(paths[index], &before[index]) == -1 ||
		    (selected == 3 ? !S_ISDIR(before[index].st_mode) :
		    !S_ISREG(before[index].st_mode)))
			return (1);
	}
	wanted = move && selected == 2 ? acl_init(0) : make_acl(selected == 3);
	if (wanted == NULL)
		return (1);
	/* Direct-child native getters never open an existing regular path. */
	if (!value_path_equals("dst", shared, binary, sizeof(binary)) ||
	    !value_path_equals("dst", empty, "", 0) ||
	    (!move && selected != 0 &&
	    !value_path_equals("dst", retained, "keep", 4)) ||
	    !(move && selected == 2 ? acl_empty_path("dst", wanted) :
	    acl_path_equals("dst", wanted)))
		goto out;
	if (move) {
		errno = 0;
		if (getxattr("dst", retained, &value, sizeof(value), 0,
		    XATTR_NOFOLLOW) != -1 || errno != ENOATTR)
			goto out;
	}
	for (index = 0; index < 2; index++) {
		if (lstat(paths[index], &after) == -1 ||
		    before[index].st_dev != after.st_dev ||
		    before[index].st_ino != after.st_ino ||
		    before[index].st_mode != after.st_mode)
			goto out;
	}
	result = 0;
out:
	saved = errno;
	(void)acl_free(wanted);
	errno = saved;
	return (result);
}

int
main(int argc, char **argv)
{
	unsigned int selected;
	int result;

	if (argc != 3)
		return (2);
	for (selected = 0; selected < sizeof(cases) / sizeof(cases[0]); selected++)
		if (strcmp(argv[2], cases[selected]) == 0)
			break;
	if (selected == sizeof(cases) / sizeof(cases[0]))
		return (2);
	if (strcmp(argv[1], "prepare") == 0)
		result = prepare(selected);
	else if (strcmp(argv[1], "observe") == 0)
		result = observe(selected, 0);
	else if (strcmp(argv[1], "observe-move") == 0)
		result = observe(selected, 1);
	else
		return (2);
	if (result != 0)
		(void)fprintf(stderr, "metadata fixture %s %s failed (errno %d)\n",
		    argv[1], argv[2], errno);
	return (result);
}
