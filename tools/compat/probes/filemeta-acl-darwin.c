/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <fcntl.h>
#include <stddef.h>

static filesec_t (*volatile allocate)(void) = filesec_init;
static void (*volatile release_security)(filesec_t) = filesec_free;
static int (*volatile query)(filesec_t, filesec_property_t, int *) =
    filesec_query_property;
static int (*volatile get)(filesec_t, filesec_property_t, void *) =
    filesec_get_property;
static int (*volatile read_fd)(int, struct stat *, filesec_t) = fstatx_np;
static int (*volatile read_link)(const char *, struct stat *, filesec_t) =
    lstatx_np;
static int (*volatile validate)(acl_t) = acl_valid;
static int (*volatile entry)(acl_t, int, acl_entry_t *) = acl_get_entry;
static int (*volatile write_fd)(int, acl_t, acl_type_t) = acl_set_fd_np;
static int (*volatile release_acl)(void *) = acl_free;

int
main(void)
{

	(void)(FILESEC_ACL + ACL_TYPE_EXTENDED + ACL_FIRST_ENTRY);
	return (allocate == NULL || release_security == NULL || query == NULL ||
	    get == NULL || read_fd == NULL || read_link == NULL ||
	    validate == NULL || entry == NULL || write_fd == NULL ||
	    release_acl == NULL);
}
