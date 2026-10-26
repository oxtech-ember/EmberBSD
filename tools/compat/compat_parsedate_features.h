/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Request native UTC declarations before headers. */
#ifndef EMBERBSD_COMPAT_PARSEDATE_FEATURES_H
#define EMBERBSD_COMPAT_PARSEDATE_FEATURES_H

/* Keep the normal compatibility guard active for caller-owned feature flags. */
#ifdef _NETBSD_SOURCE
#error _NETBSD_SOURCE is not permitted by the host compatibility policy.
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#if defined(__NetBSD__)
/* Load base types under the normal restricted compatibility namespace. */
#ifndef _ISOC99_SOURCE
#define _ISOC99_SOURCE
#endif
#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#endif
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#if _POSIX_C_SOURCE != 200112L || _XOPEN_SOURCE != 600
#error The date adapter requires the normal restricted compatibility prefix.
#endif
#include <sys/types.h>
/* sys/time.h would load time.h too early, before its native UTC declarations. */
#define _NETBSD_SOURCE 1
#include <time.h>
#undef _NETBSD_SOURCE
#else
#include <time.h>
#endif

#endif
