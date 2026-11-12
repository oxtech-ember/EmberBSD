/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Linux 6.18 include/linux/limits.h (GPL-2.0) interface; values are the C
 * limits of the fixed-width types.  U16..S64 limits are also defined by
 * <linux/kernel.h>, so each is guarded.
 */

#ifndef _LINUX_LIMITS_H_
#define _LINUX_LIMITS_H_

#include <sys/param.h>
#include <sys/types.h>

#include <linux/kernel.h>

#ifndef U8_MAX
#define	U8_MAX	UINT8_MAX
#endif
#ifndef S8_MAX
#define	S8_MAX	INT8_MAX
#endif
#ifndef S8_MIN
#define	S8_MIN	INT8_MIN
#endif
#ifndef U16_MAX
#define	U16_MAX	UINT16_MAX
#endif
#ifndef S16_MAX
#define	S16_MAX	INT16_MAX
#endif
#ifndef S16_MIN
#define	S16_MIN	INT16_MIN
#endif
#ifndef U32_MAX
#define	U32_MAX	UINT32_MAX
#endif
#ifndef S32_MAX
#define	S32_MAX	INT32_MAX
#endif
#ifndef S32_MIN
#define	S32_MIN	INT32_MIN
#endif
#ifndef U64_MAX
#define	U64_MAX	UINT64_MAX
#endif
#ifndef S64_MAX
#define	S64_MAX	INT64_MAX
#endif
#ifndef S64_MIN
#define	S64_MIN	INT64_MIN
#endif

#endif	/* _LINUX_LIMITS_H_ */
