/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Native bootstrap source annotations. */
#ifndef EMBERBSD_BOOTSTRAP_CDEFS_H
#define EMBERBSD_BOOTSTRAP_CDEFS_H

#include <sys/cdefs.h>

#define EMBERBSD_BOOTSTRAP_ID_INNER(prefix, line) prefix ## line
#define EMBERBSD_BOOTSTRAP_ID(prefix, line) EMBERBSD_BOOTSTRAP_ID_INNER(prefix, line)

#ifndef __RCSID
#define __RCSID(text) \
    static const char EMBERBSD_BOOTSTRAP_ID(emberbsd_rcsid_, __LINE__)[] \
    __attribute__((__used__)) = text
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(text) \
    static const char EMBERBSD_BOOTSTRAP_ID(emberbsd_copyright_, __LINE__)[] \
    __attribute__((__used__)) = text
#endif
#ifndef __dead
#define __dead __attribute__((__noreturn__))
#endif

#endif /* EMBERBSD_BOOTSTRAP_CDEFS_H */
