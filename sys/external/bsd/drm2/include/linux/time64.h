/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/time64.h (GPL-2.0).  time.h already
 * has NSEC_PER_SEC, NSEC_PER_MSEC, NSEC_PER_USEC and USEC_PER_MSEC and maps
 * timespec64 onto struct timespec.
 */

#ifndef _LINUX_TIME64_H_
#define _LINUX_TIME64_H_

#include <linux/time.h>
#include <linux/types.h>

typedef int64_t	time64_t;

#ifndef MSEC_PER_SEC
#define	MSEC_PER_SEC	1000L
#endif
#ifndef USEC_PER_SEC
#define	USEC_PER_SEC	1000000L
#endif
#ifndef PSEC_PER_NSEC
#define	PSEC_PER_NSEC	1000L
#endif
#ifndef NSEC_PER_MSEC
#define	NSEC_PER_MSEC	1000000L
#endif

#define	ns_to_timespec64	ns_to_timespec
#define	timespec64_to_ns	timespec_to_ns

#endif	/* _LINUX_TIME64_H_ */
