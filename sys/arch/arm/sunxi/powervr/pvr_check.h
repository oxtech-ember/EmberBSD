/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 Imagination Technologies Ltd. */
/* Native C adaptation: see README.md and LICENSE. */
#ifndef PVR_CHECK_H
#define PVR_CHECK_H

#include "pvr_fw_abi.h"

#define OFFSET_CHECK(type, member, offset) \
	_Static_assert(offsetof(type, member) == (offset), \
	"offsetof(" #type ", " #member ") incorrect")

#define SIZE_CHECK(type, size) \
	_Static_assert(sizeof(type) == (size), #type " is incorrect size")

#define ALIGN_CHECK(type, align) \
	_Static_assert(__alignof__(type) <= (align), #type " has incorrect alignment")

/*
 * The upstream check calls flex_array_size(..., 1). Its count cannot overflow.
 * The native form checks the array type directly. It also rejects pointers.
 * As in upstream, a fixed-size array is compatible with the incomplete type.
 */
#define FLEX_ARRAY_CHECK(type, member) \
	_Static_assert(__builtin_types_compatible_p( \
	    __typeof__(((type *)0)->member), \
	    __typeof__(((type *)0)->member[0])[]), \
	    #type "->" #member " is incorrect size")

#endif /* PVR_CHECK_H */
