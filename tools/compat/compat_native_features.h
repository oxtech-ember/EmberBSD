/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
#ifndef EMBERBSD_COMPAT_NATIVE_FEATURES_H
#define EMBERBSD_COMPAT_NATIVE_FEATURES_H

#if defined(__NetBSD__)
/* Preserve the normal configuration's types and fallback declarations. */
#ifdef _NETBSD_SOURCE
#error _NETBSD_SOURCE is not permitted by the host compatibility policy.
#endif
#if !defined(_POSIX_SOURCE) || _POSIX_C_SOURCE != 200112L || \
    _XOPEN_SOURCE != 600
#error Include the native adapter after the normal NetBSD configuration.
#endif
#include <sys/types.h>
#include <sys/endian.h>
#include <sys/time.h>
#include <sys/param.h>
#endif /* __NetBSD__ */

#endif /* EMBERBSD_COMPAT_NATIVE_FEATURES_H */
