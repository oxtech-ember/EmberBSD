/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#ifndef EMBERBSD_COMPAT_ACL_FEATURES_H
#define EMBERBSD_COMPAT_ACL_FEATURES_H

#if defined(__NetBSD__)
/* Use only after the ordinary configuration has selected its namespace. */
#ifdef _NETBSD_SOURCE
#error _NETBSD_SOURCE is not permitted by the host compatibility policy.
#endif
#if !defined(_POSIX_SOURCE) || _POSIX_C_SOURCE != 200112L || \
    _XOPEN_SOURCE != 600
#error Include the ACL adapter after the normal NetBSD configuration.
#endif

/* Keep endian bodies and other fallback declarations in the normal namespace. */
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/time.h>
#include <sys/param.h>

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
