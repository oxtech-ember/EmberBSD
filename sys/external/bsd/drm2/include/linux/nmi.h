/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/nmi.h (GPL-2.0).  There is no NMI
 * watchdog to feed in the layer, so touch_nmi_watchdog() does nothing.
 */

#ifndef _LINUX_NMI_H_
#define _LINUX_NMI_H_

static inline void
touch_nmi_watchdog(void)
{
}

#endif	/* _LINUX_NMI_H_ */
