/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD (AI-assisted). Typed host and target stat access. */
#ifndef LS_STAT_H
#define LS_STAT_H

#if HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_ATIM
#define LS_ATIMENSEC(st) ((st)->st_atim.tv_nsec)
#define LS_MTIMENSEC(st) ((st)->st_mtim.tv_nsec)
#define LS_CTIMENSEC(st) ((st)->st_ctim.tv_nsec)
#elif defined(__NetBSD__) || \
    (HAVE_NBTOOL_CONFIG_H && HAVE_STRUCT_STAT_ST_MTIMENSEC)
#define LS_ATIMENSEC(st) ((st)->st_atimensec)
#define LS_MTIMENSEC(st) ((st)->st_mtimensec)
#define LS_CTIMENSEC(st) ((st)->st_ctimensec)
#else
#define LS_ATIMENSEC(st) ((st)->st_atimespec.tv_nsec)
#define LS_MTIMENSEC(st) ((st)->st_mtimespec.tv_nsec)
#define LS_CTIMENSEC(st) ((st)->st_ctimespec.tv_nsec)
#endif

#endif /* LS_STAT_H */
