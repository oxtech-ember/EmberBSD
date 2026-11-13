/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * debugfs for the 6.18 core: there is none on EmberBSD.  Files are not
 * created (the functions that create them return NULL, their arguments are
 * not evaluated); a printer into a seq_file still works (see seq_file.h).
 * Shadow of the shared header, which is empty.
 */

#ifndef _DRM618_LINUX_DEBUGFS_H_
#define _DRM618_LINUX_DEBUGFS_H_

#include <linux/types.h>
#include <linux/seq_file.h>

struct dentry;
struct device;

#define	debugfs_create_file(...)	((struct dentry *)NULL)
#define	debugfs_create_dir(...)		((struct dentry *)NULL)
#define	debugfs_remove(dentry)		((void)0)
#define	debugfs_remove_recursive(d)	((void)0)
#define	debugfs_lookup(...)		((struct dentry *)NULL)

struct debugfs_reg32 {
	const char	*name;
	unsigned long	offset;
};

struct debugfs_regset32 {
	const struct debugfs_reg32	*regs;
	int				nregs;
	void __iomem			*base;
	struct device			*dev;
};

#endif	/* _DRM618_LINUX_DEBUGFS_H_ */
