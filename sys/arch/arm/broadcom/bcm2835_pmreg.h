/*-
 * Copyright (c) 2012, 2016 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Nick Hudson
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
/* Broadcom power-management and watchdog register interface. */

#ifndef _ARM_BROADCOM_BCM2835_PMREG_H_
#define _ARM_BROADCOM_BCM2835_PMREG_H_

#define BCM2712_PM_BASE UINT64_C(0x107d200000)

#ifndef BCM2835_PM_DEFAULT_PERIOD
#define BCM2835_PM_DEFAULT_PERIOD	15	/* seconds */
#endif

#define	 BCM2835_PM_PASSWORD		0x5a000000
#define	 BCM2835_PM_PASSWORD_MASK	0xff000000

#define	BCM2835_PM_RSTC		0x1c
#define	 BCM2835_PM_RSTC_CONFIGMASK	0x00000030
#define	 BCM2835_PM_RSTC_FULL_RESET	0x00000020
#define	 BCM2835_PM_RSTC_RESET		0x00000102

#define	BCM2835_PM_RSTS		0x20
#define  BCM2835_PM_RSTS_PART(x) \
		((uint16_t)((x) & 0x01) << 0 | (uint16_t)((x) & 0x02) << 1 | \
		 (uint16_t)((x) & 0x04) << 2 | (uint16_t)((x) & 0x08) << 3 | \
		 (uint16_t)((x) & 0x10) << 4 | (uint16_t)((x) & 0x20) << 5)

#define	BCM2835_PM_WDOG		0x24
#define	 BCM2835_PM_WDOG_TIMEMASK	0x000fffff

#endif
