/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#define EMBERBSD_FILEMETA_ACL_PROBE 1
#include "compat_filemeta_features.h"
#include <sys/types.h>
#include <sys/acl.h>
#include <stddef.h>
#include <unistd.h>

static long (*volatile query_fd)(int, int) = fpathconf;
static long (*volatile query_link)(const char *, int) = lpathconf;
static acl_t (*volatile read_fd)(int, acl_type_t) = acl_get_fd_np;
static acl_t (*volatile read_link)(const char *, acl_type_t) = acl_get_link_np;
static int (*volatile write_fd)(int, acl_t, acl_type_t) = acl_set_fd_np;
static int (*volatile write_link)(const char *, acl_type_t, acl_t) =
    acl_set_link_np;
static int (*volatile trivial)(acl_t, int *) = acl_is_trivial_np;
static int (*volatile entry)(acl_t, int, acl_entry_t *) = acl_get_entry;
static int (*volatile release_acl)(void *) = acl_free;

int
main(void)
{

	(void)(_PC_ACL_NFS4 + _PC_ACL_EXTENDED + ACL_TYPE_NFS4 +
	    ACL_TYPE_ACCESS + ACL_TYPE_DEFAULT + ACL_FIRST_ENTRY);
	return (query_fd == NULL || query_link == NULL || read_fd == NULL ||
	    read_link == NULL || write_fd == NULL || write_link == NULL ||
	    trivial == NULL || entry == NULL || release_acl == NULL);
}
