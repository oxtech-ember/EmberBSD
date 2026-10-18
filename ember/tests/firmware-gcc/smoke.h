/* SPDX-License-Identifier: BSD-2-Clause */
/* Origin: EmberBSD, AI-assisted freestanding compiler contract. */
#ifndef FIRMWARE_GCC_SMOKE_H
#define FIRMWARE_GCC_SMOKE_H

#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(FIRMWARE_STRICT) && defined(__NetBSD__)
#error "The firmware profile must not select NetBSD standard headers"
#endif
#if defined(FIRMWARE_STRICT) && __STDC_HOSTED__ != 0
#error "The firmware profile must use the freestanding C environment"
#endif
#ifndef FIRMWARE_HOST_TEST
#ifndef __aarch64__
#error "The target must retain the AArch64 architecture macro"
#endif
#if __SIZEOF_POINTER__ != 8 || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error "The target must retain the little-endian LP64 ABI"
#endif
#endif

_Static_assert(sizeof(size_t) == 8, "size_t must match the LP64 ABI");
_Static_assert(sizeof(uint64_t) == 8, "uint64_t must remain available");

uint64_t firmware_calculate(uint64_t);
#endif
