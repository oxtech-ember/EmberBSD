/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/* Shared host ACL presence; no ACL storage layout is exposed. */
#ifndef EMBERBSD_COMPAT_ACL_H
#define EMBERBSD_COMPAT_ACL_H
#include <sys/cdefs.h>

__BEGIN_DECLS
/* Success sets *present to 0 or 1. Failure preserves a native errno. */
int __nbcompat_acl_present_nofollow(const char *, int *);
__END_DECLS

#endif /* EMBERBSD_COMPAT_ACL_H */
