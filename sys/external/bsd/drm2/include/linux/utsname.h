/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/utsname.h (GPL-2.0): init_utsname()
 * describes the running system from the kernel's ostype, osrelease, version
 * string and machine.  The result is a static buffer rebuilt on every call.
 */

#ifndef _LINUX_UTSNAME_H_
#define _LINUX_UTSNAME_H_

#include <sys/param.h>
#include <sys/systm.h>

#define	__NEW_UTS_LEN	64

struct new_utsname {
	char	sysname[__NEW_UTS_LEN + 1];
	char	nodename[__NEW_UTS_LEN + 1];
	char	release[__NEW_UTS_LEN + 1];
	char	version[__NEW_UTS_LEN + 1];
	char	machine[__NEW_UTS_LEN + 1];
	char	domainname[__NEW_UTS_LEN + 1];
};

static inline struct new_utsname *
init_utsname(void)
{
	static struct new_utsname uts;

	strlcpy(uts.sysname, ostype, sizeof(uts.sysname));
	strlcpy(uts.nodename, hostname, sizeof(uts.nodename));
	strlcpy(uts.release, osrelease, sizeof(uts.release));
	strlcpy(uts.version, kernel_ident, sizeof(uts.version));
	strlcpy(uts.machine, machine, sizeof(uts.machine));
	strlcpy(uts.domainname, "(none)", sizeof(uts.domainname));
	return &uts;
}

#endif	/* _LINUX_UTSNAME_H_ */
