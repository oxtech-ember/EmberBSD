/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD native host shim; stands in for the layer's <linux/types.h>. */
#ifndef DRM2_COMPAT_SHIM_LINUX_TYPES_H
#define DRM2_COMPAT_SHIM_LINUX_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

#ifndef __unused
#define __unused __attribute__((__unused__))
#endif
#ifndef __always_inline
#define __always_inline inline __attribute__((__always_inline__))
#endif
#ifndef __printflike
#define __printflike(a, b) __attribute__((__format__(__printf__, a, b)))
#endif

#endif
