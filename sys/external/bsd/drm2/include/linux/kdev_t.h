/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/kdev_t.h (GPL-2.0): the Linux
 * device-number encoding, 12 bits of major over 20 bits of minor.  These are
 * Linux numbers, not NetBSD dev_t values.
 */

#ifndef _LINUX_KDEV_T_H_
#define _LINUX_KDEV_T_H_

#define	MINORBITS	20
#define	MINORMASK	((1U << MINORBITS) - 1)

#define	MAJOR(DEV)	((unsigned int)((DEV) >> MINORBITS))
#define	MINOR(DEV)	((unsigned int)((DEV) & MINORMASK))
#define	MKDEV(MA, MI)	(((unsigned int)(MA) << MINORBITS) | (unsigned int)(MI))

#endif	/* _LINUX_KDEV_T_H_ */
