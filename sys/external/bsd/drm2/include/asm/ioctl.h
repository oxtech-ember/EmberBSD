/*	$NetBSD: ioctl.h,v 1.2 2014/03/18 18:20:42 riastradh Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Taylor R. Campbell.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _ASM_IOCTL_H_
#define _ASM_IOCTL_H_

#include <sys/ioccom.h>

/*
 * Decoding of the request numbers made by _IO/_IOR/_IOW/_IOWR, with the
 * encoding of NetBSD's <sys/ioccom.h> (number in bits 0-7, group in 8-15,
 * parameter size above, direction in the top three bits).  Integer
 * constant expressions, as they are used as array designators.
 * EmberBSD.
 */
#ifndef _IOC_NR
#define	_IOC_NR(x)	((x) & 0xff)
#define	_IOC_TYPE(x)	(((x) >> 8) & 0xff)
#define	_IOC_SIZE(x)	IOCPARM_LEN(x)
#define	_IOC_DIR(x)	((x) & IOC_DIRMASK)
#endif

#endif  /* _ASM_IOCTL_H_ */
