/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/* Canonical formatting and real private no-follow ACL observations. */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <errno.h>
#include <fcntl.h>
#include <membership.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include "compat_listing.h"
#include "compat_acl.h"

static int
formatting(void)
{
	char buf[32], flagsbuf[] = "nodump,uchg";
	char *flags, *cursor;
	unsigned long set, clear;
	long block;
	int length;

	if (unsetenv("BLOCKSIZE") != 0 ||
	    strcmp(getbsize(&length, &block), "512-blocks") != 0 ||
	    block != 512 || length != 10)
		return (1);
	if (setenv("BLOCKSIZE", "512", 1) != 0 ||
	    strcmp(getbsize(&length, &block), "512-blocks") != 0 ||
	    block != 512 || length != 10 ||
	    setenv("BLOCKSIZE", "1K", 1) != 0 ||
	    strcmp(getbsize(&length, &block), "1K-blocks") != 0 ||
	    block != 1024 || length != 9)
		return (2);
	if (setenv("BLOCKSIZE", "2M", 1) != 0 ||
	    strcmp(getbsize(&length, &block), "2M-blocks") != 0 ||
	    block != 2 * 1024 * 1024 || length != 9)
		return (2);
	/* Expected values come from tests/lib/libc/gen/t_humanize_number.c. */
	if (humanize_number(buf, 5, 1, "", 0, HN_B) != 3 ||
	    strcmp(buf, "1 B") != 0 ||
	    humanize_number(buf, 5, 1, "", 0, HN_NOSPACE | HN_B) != 2 ||
	    strcmp(buf, "1B") != 0 ||
	    humanize_number(buf, 10, 10000, "", HN_AUTOSCALE,
	    HN_NOSPACE) != 5 ||
	    strcmp(buf, "10000") != 0 ||
	    humanize_number(buf, 6, 1000, "A", HN_AUTOSCALE,
	    HN_DECIMAL) != -1 ||
	    humanize_number(buf, 5, 1048258238, "", HN_AUTOSCALE,
	    HN_B | HN_NOSPACE | HN_DECIMAL) != 4 || strcmp(buf, "1.0G") != 0)
		return (3);
	if (humanize_number(buf, 5, 1000, "", HN_GETSCALE, HN_B) != 1 ||
	    buf[0] != '\0' ||
	    humanize_number(buf, sizeof(buf), -42, "", 0, HN_NOSPACE) != 3 ||
	    strcmp(buf, "-42") != 0 ||
	    humanize_number(buf, 5, INT64_C(10737418236) * 1024, "",
	    HN_AUTOSCALE, HN_B | HN_NOSPACE | HN_DECIMAL) != 3 ||
	    strcmp(buf, "10T") != 0)
		return (3);
	flags = flags_to_string(0, "-");
	if (flags == NULL)
		return (4);
	if (strcmp(flags, "-") != 0) {
		free(flags);
		return (5);
	}
	free(flags);
	flags = flags_to_string(UF_NODUMP | UF_IMMUTABLE, "-");
	if (flags == NULL)
		return (6);
	if (strcmp(flags, "uchg,nodump") != 0) {
		free(flags);
		return (7);
	}
	free(flags);
	cursor = flagsbuf;
	if (string_to_flags(&cursor, &set, &clear) != 0 || clear != 0 ||
	    set != (UF_NODUMP | UF_IMMUTABLE))
		return (8);
	cursor = buf;
	(void)strcpy(buf, "unknown-flag");
	if (string_to_flags(&cursor, &set, &clear) != 1 ||
	    strcmp(cursor, "unknown-flag") != 0)
		return (9);
	return (0);
}

static int
acls(void)
{
	acl_t acl = NULL, observed = NULL;
	acl_entry_t entry;
	acl_permset_t perms;
	uuid_t owner;
	struct stat before, after;
	int fd = -1, present = -1, result = 1, saved;
	int file_created = 0, link_created = 0;

	fd = open("listing-file", O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd == -1)
		goto out;
	file_created = 1;
	acl = acl_init(0);
	if (acl == NULL || acl_set_fd_np(fd, acl, ACL_TYPE_EXTENDED) != 0 ||
	    __nbcompat_acl_present_nofollow("listing-file", &present) != 0 ||
	    present != 0)
		goto out;
	if (mbr_uid_to_uuid(getuid(), owner) != 0 ||
	    acl_create_entry(&acl, &entry) != 0 ||
	    acl_set_tag_type(entry, ACL_EXTENDED_ALLOW) != 0 ||
	    acl_set_qualifier(entry, owner) != 0 ||
	    acl_get_permset(entry, &perms) != 0 ||
	    acl_clear_perms(perms) != 0 ||
	    acl_add_perm(perms, ACL_READ_DATA) != 0 ||
	    acl_set_permset(entry, perms) != 0 ||
	    acl_set_fd_np(fd, acl, ACL_TYPE_EXTENDED) != 0)
		goto out;
	/* An independent native fd query proves the fixture is not empty. */
	observed = acl_get_fd_np(fd, ACL_TYPE_EXTENDED);
	if (observed == NULL ||
	    acl_get_entry(observed, ACL_FIRST_ENTRY, &entry) != 0 ||
	    __nbcompat_acl_present_nofollow("listing-file", &present) != 0 ||
	    present != 1 || fstat(fd, &before) != 0)
		goto out;
	if (symlink("listing-file", "listing-link") != 0)
		goto out;
	link_created = 1;
	if (__nbcompat_acl_present_nofollow("listing-link", &present) != 0 ||
	    present != 0 || fstat(fd, &after) != 0 ||
	    before.st_dev != after.st_dev || before.st_ino != after.st_ino ||
	    before.st_mode != after.st_mode ||
	    before.st_size != after.st_size ||
	    __nbcompat_acl_present_nofollow("listing-file", &present) != 0 ||
	    present != 1)
		goto out;
	present = 7;
	errno = 0;
	if (__nbcompat_acl_present_nofollow("listing-missing",
	    &present) != -1 ||
	    errno != ENOENT || present != 7)
		goto out;
	result = 0;
out:
	saved = errno;
	if (observed != NULL)
		(void)acl_free(observed);
	if (acl != NULL)
		(void)acl_free(acl);
	if (fd != -1 && close(fd) != 0)
		result = 1;
	if (link_created && unlink("listing-link") != 0)
		result = 1;
	if (file_created && unlink("listing-file") != 0)
		result = 1;
	if (result != 0)
		(void)fprintf(stderr, "listing ACL check failed (errno %d)\n",
		    saved);
	return (result);
}

int
main(void)
{
	int result;

	result = formatting();
	if (result != 0) {
		(void)fprintf(stderr, "listing formatting case %d failed\n",
		    result);
		return (1);
	}
	return (acls());
}
