/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). */
/* Host-only declarations for the complete libc/libutil reachover units. */
#ifndef EMBERBSD_COMPAT_LISTING_H
#define EMBERBSD_COMPAT_LISTING_H

#include <sys/types.h>
#include <stdint.h>
#include <stdlib.h>

/* These names keep native host variants out of the canonical ABI. */
#define getbsize __nbcompat_getbsize
#define humanize_number __nbcompat_humanize_number
#define flags_to_string __nbcompat_flags_to_string
#define string_to_flags __nbcompat_string_to_flags

/* Values and declarations follow include/stdlib.h and include/util.h. */
#undef HN_DECIMAL
#undef HN_NOSPACE
#undef HN_B
#undef HN_DIVISOR_1000
#undef HN_GETSCALE
#undef HN_AUTOSCALE
#define HN_DECIMAL 0x01
#define HN_NOSPACE 0x02
#define HN_B 0x04
#define HN_DIVISOR_1000 0x08
#define HN_GETSCALE 0x10
#define HN_AUTOSCALE 0x20

/* Match the existing normal host diagnostic-assert policy. */
#ifndef _DIAGASSERT
#define _DIAGASSERT(x)
#endif

__BEGIN_DECLS
char *getbsize(int *, long *);
int humanize_number(char *, size_t, int64_t, const char *, int, int);
char *flags_to_string(unsigned long, const char *);
int string_to_flags(char **, unsigned long *, unsigned long *);
__END_DECLS

#endif /* EMBERBSD_COMPAT_LISTING_H */
