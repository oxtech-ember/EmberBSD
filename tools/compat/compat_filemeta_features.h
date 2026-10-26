/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#ifndef EMBERBSD_COMPAT_FILEMETA_FEATURES_H
#define EMBERBSD_COMPAT_FILEMETA_FEATURES_H

#if defined(__NetBSD__)
/* The common prefix owns restricted types and endian fallback declarations. */
#include "compat_native_features.h"
#if HAVE_FILEMETA_ACL_BSD || defined(EMBERBSD_FILEMETA_ACL_PROBE)
#include "compat_acl_features.h"
#endif
#ifdef _NETBSD_SOURCE
#error _NETBSD_SOURCE is not permitted by the host compatibility policy.
#endif
/* Native no-follow and regular-only open flags also need this namespace. */
#define _NETBSD_SOURCE 1
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/extattr.h>
#undef _NETBSD_SOURCE
#ifndef O_NOFOLLOW
#error Include the metadata adapter before native fcntl headers.
#endif
#endif /* __NetBSD__ */

#endif /* EMBERBSD_COMPAT_FILEMETA_FEATURES_H */
