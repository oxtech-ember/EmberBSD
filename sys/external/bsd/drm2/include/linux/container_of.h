/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Linux 6.18 include/linux/container_of.h (GPL-2.0) interface: container_of()
 * comes from <sys/container_of.h>; container_of_const() keeps the constness
 * of the member pointer in the result.
 */

#ifndef _LINUX_CONTAINER_OF_H_
#define _LINUX_CONTAINER_OF_H_

#include <sys/container_of.h>

#include <linux/kernel.h>

/*
 * Both _Generic branches are type-checked even when not selected, so the
 * offset is applied through uintptr_t (a cast between char pointers would
 * trip -Wcast-qual for the unselected branch).  The pointer subtraction only
 * checks that PTR has the type of FIELD, ignoring qualifiers.
 */
#define	__container_of_addr(PTR, TYPE, FIELD)				\
	((uintptr_t)(PTR) - offsetof(TYPE, FIELD))
#define	__container_of_check(PTR, TYPE, FIELD)				\
	(0 * sizeof((PTR) - &((const TYPE *)0)->FIELD))

#define	container_of_const(PTR, TYPE, FIELD)				\
	_Generic(PTR,							\
	    const __typeof__(*(PTR)) *:					\
		((const TYPE *)__container_of_addr(PTR, TYPE, FIELD)	\
		    + __container_of_check(PTR, TYPE, FIELD)),		\
	    default:							\
		((TYPE *)__container_of_addr(PTR, TYPE, FIELD)		\
		    + __container_of_check(PTR, TYPE, FIELD)))

#endif	/* _LINUX_CONTAINER_OF_H_ */
