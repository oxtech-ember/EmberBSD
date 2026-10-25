/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Shared file-time declarations. */
#ifndef EMBERBSD_COMPAT_FILETIME_H
#define EMBERBSD_COMPAT_FILETIME_H

#include <sys/stat.h>

__BEGIN_DECLS
int utimens(const char *, const struct timespec [2]);
int lutimens(const char *, const struct timespec [2]);
__END_DECLS

#endif
