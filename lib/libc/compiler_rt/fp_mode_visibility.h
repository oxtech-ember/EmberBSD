/* Copyright (c) 2026 EmberBSD contributors. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD; keep compiler-rt floating-point helpers private. */
#ifndef _LIBC_COMPILER_RT_FP_MODE_VISIBILITY_H_
#define _LIBC_COMPILER_RT_FP_MODE_VISIBILITY_H_

#include <fp_mode.h>

extern __typeof__(__fe_getround) __fe_getround
    __attribute__((__visibility__("hidden")));
extern __typeof__(__fe_raise_inexact) __fe_raise_inexact
    __attribute__((__visibility__("hidden")));

#endif
