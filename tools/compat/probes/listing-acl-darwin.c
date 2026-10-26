/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <fcntl.h>
#include <stddef.h>

static filesec_t (*volatile create)(void) = filesec_init;
static void (*volatile destroy)(filesec_t) = filesec_free;
static int (*volatile query)(filesec_t, filesec_property_t, int *) =
    filesec_query_property;
static int (*volatile get)(filesec_t, filesec_property_t, void *) =
    filesec_get_property;
static int (*volatile status)(const char *, struct stat *, filesec_t) =
    lstatx_np;
static int (*volatile valid)(acl_t) = acl_valid;
static int (*volatile first)(acl_t, int, acl_entry_t *) = acl_get_entry;
static int (*volatile release)(void *) = acl_free;

int
main(void)
{
	(void)(FILESEC_ACL + ACL_FIRST_ENTRY + ACL_TYPE_EXTENDED);
	return (create == NULL || destroy == NULL || query == NULL ||
	    get == NULL || status == NULL || valid == NULL || first == NULL ||
	    release == NULL);
}
