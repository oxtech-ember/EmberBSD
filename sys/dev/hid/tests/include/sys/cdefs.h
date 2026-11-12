/* SPDX-License-Identifier: BSD-2-Clause */
/* Host libc definitions plus NetBSD annotations used by the HID sources. */
#include_next <sys/cdefs.h>
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, s)
#endif
#ifndef __packed
#define __packed __attribute__((__packed__))
#endif
#ifndef __CTASSERT
#define __CTASSERT(e) _Static_assert(e, #e)
#endif
