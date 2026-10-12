/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef PVR_FW_ABI_H
#define PVR_FW_ABI_H

#include <sys/types.h>
#ifdef _KERNEL
#include <sys/stdbool.h>
#include <sys/stddef.h>
#else
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#endif

#define PVR_FW_ALIGNED(n) __attribute__((__aligned__(n)))
#define PVR_FW_BIT(n) (1UL << (n))
#define PVR_FW_ALIGN(x, a) \
	(((x) + (__typeof__(x))((a) - 1)) & ~(__typeof__(x))((a) - 1))
typedef uint64_t pvr_fw_aligned_u64 PVR_FW_ALIGNED(8);

/* This imported host ABI has native pointers in trace-buffer records. */
_Static_assert(sizeof(void *) == 8, "PowerVR FWIF requires 64-bit host pointers");
_Static_assert(sizeof(bool) == 1, "PowerVR FWIF requires one-byte bool");
_Static_assert(sizeof(uint32_t) == 4, "PowerVR FWIF requires four-byte uint32_t");
_Static_assert(sizeof(uint64_t) == 8, "PowerVR FWIF requires eight-byte uint64_t");
_Static_assert(_Alignof(pvr_fw_aligned_u64) == 8, "PowerVR FWIF requires u64 alignment");
#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__)
#error PowerVR FWIF requires compiler byte-order definitions
#elif __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__
#error PowerVR FWIF currently supports little-endian hosts only
#endif

#endif /* PVR_FW_ABI_H */
