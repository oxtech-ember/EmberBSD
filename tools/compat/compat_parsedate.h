/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Shared date parser declaration. */
#ifndef EMBERBSD_COMPAT_PARSEDATE_H
#define EMBERBSD_COMPAT_PARSEDATE_H

#include <sys/cdefs.h>
#include <time.h>

__BEGIN_DECLS
time_t parsedate(const char *, const time_t *, const int *);
__END_DECLS

#endif
