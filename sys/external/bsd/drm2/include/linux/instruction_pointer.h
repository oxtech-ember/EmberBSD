/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  The address of the point
 * of the call, for the Linux 6.18 DRM core (drm_print.h).  Written to the
 * interface; uses the label-address extension of clang and gcc.
 */
#ifndef _LINUX_INSTRUCTION_POINTER_H_
#define _LINUX_INSTRUCTION_POINTER_H_

#define	_THIS_IP_	({ __label__ __here; __here: (unsigned long)&&__here; })

#endif
