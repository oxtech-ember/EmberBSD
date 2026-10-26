/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/acl.h>
#include <stddef.h>
#include <unistd.h>

static long (*volatile query)(const char *, int) = lpathconf;
static acl_t (*volatile get_acl)(const char *, acl_type_t) = acl_get_link_np;
static int (*volatile trivial)(acl_t, int *) = acl_is_trivial_np;
static int (*volatile release)(void *) = acl_free;

int
main(void)
{
	(void)(_PC_ACL_NFS4 + _PC_ACL_EXTENDED +
	    ACL_TYPE_NFS4 + ACL_TYPE_ACCESS);
	(void)S_ISWHT(0);
	return (query == NULL || get_acl == NULL || trivial == NULL ||
	    release == NULL);
}
