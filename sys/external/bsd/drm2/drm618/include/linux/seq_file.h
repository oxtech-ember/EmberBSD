/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Shadow of the shared <linux/seq_file.h> for the 6.18 core: the macros that
 * make a debugfs/proc file out of a show function.  There is no debugfs, so
 * the file operations structure is not made; the show function stays
 * referenced (no unused-function warning) and can be called with a
 * seq_file made by seq_file_init().
 */

#ifndef _DRM618_LINUX_SEQ_FILE_H_
#define _DRM618_LINUX_SEQ_FILE_H_

#include_next <linux/seq_file.h>

#define	DEFINE_SHOW_ATTRIBUTE(name)					\
	static int (* const name##_fops_show)(struct seq_file *, void *) \
	    __attribute__((__unused__)) = name##_show

#define	DEFINE_SHOW_STORE_ATTRIBUTE(name)	DEFINE_SHOW_ATTRIBUTE(name)

#endif	/* _DRM618_LINUX_SEQ_FILE_H_ */
