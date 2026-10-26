/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#ifndef EMBERBSD_COMPAT_ACL_FEATURES_H
#define EMBERBSD_COMPAT_ACL_FEATURES_H

#if defined(__NetBSD__)
#include "compat_native_features.h"
#ifdef _NETBSD_SOURCE
#error _NETBSD_SOURCE is not permitted by the host compatibility policy.
#endif

/* These native ACL interfaces have no POSIX declaration. */
#define _NETBSD_SOURCE 1
#include <sys/stat.h>
#include <sys/acl.h>
#include <unistd.h>
#undef _NETBSD_SOURCE

#if !defined(_PC_ACL_NFS4) || !defined(_PC_ACL_EXTENDED) || !defined(S_ISWHT)
#error Include the ACL adapter before native stat and unistd headers.
#endif
#endif /* __NetBSD__ */

#endif /* EMBERBSD_COMPAT_ACL_FEATURES_H */
