/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include "compat_filemeta_features.h"
#include <sys/types.h>
#include <sys/extattr.h>
#include <stddef.h>

static ssize_t (*volatile list_fd)(int, int, void *, size_t) = extattr_list_fd;
static ssize_t (*volatile list_link)(const char *, int, void *, size_t) =
    extattr_list_link;
static ssize_t (*volatile get_fd)(int, int, const char *, void *, size_t) =
    extattr_get_fd;
static ssize_t (*volatile get_link)(const char *, int, const char *, void *,
    size_t) = extattr_get_link;
static NB_FILEMETA_SET_RESULT (*volatile set_fd)(int, int, const char *,
    const void *, size_t) = extattr_set_fd;
static NB_FILEMETA_SET_RESULT (*volatile set_link)(const char *, int,
    const char *, const void *, size_t) = extattr_set_link;

int
main(void)
{

	(void)(EXTATTR_NAMESPACE_USER + EXTATTR_NAMESPACE_SYSTEM);
	return (list_fd == NULL || list_link == NULL || get_fd == NULL ||
	    get_link == NULL || set_fd == NULL || set_link == NULL);
}
