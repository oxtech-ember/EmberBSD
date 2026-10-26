/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <sys/xattr.h>
#include <stddef.h>
#include <stdint.h>

static ssize_t (*volatile list_fd)(int, char *, size_t, int) = flistxattr;
static ssize_t (*volatile list_link)(const char *, char *, size_t, int) =
    listxattr;
static ssize_t (*volatile get_fd)(int, const char *, void *, size_t,
    uint32_t, int) = fgetxattr;
static ssize_t (*volatile get_link)(const char *, const char *, void *,
    size_t, uint32_t, int) = getxattr;
static int (*volatile set_fd)(int, const char *, const void *, size_t,
    uint32_t, int) = fsetxattr;
static int (*volatile set_link)(const char *, const char *, const void *,
    size_t, uint32_t, int) = setxattr;

int
main(void)
{

	(void)XATTR_NOFOLLOW;
	return (list_fd == NULL || list_link == NULL || get_fd == NULL ||
	    get_link == NULL || set_fd == NULL || set_link == NULL);
}
