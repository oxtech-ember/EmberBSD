/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.
 *
 * Compiled into every file of the Linux 6.18 DRM core (-include in
 * files.drmkms618, and in the compile map).  The files of dist618 are byte
 * for byte as upstream and, like all Linux code, rely on headers that reach
 * them through other Linux headers (a 6.18 DRM header uses struct completion
 * or wait_queue_head_t without including their headers, because Linux's own
 * chain of includes brings them).  The shared Linux layer is not shaped like
 * that chain, and its headers serve the 5.6 core as well, so the includes the
 * 6.18 sources expect are collected here instead of editing the layer or
 * dist618.  Every line says what needs it; a line goes when the layer
 * provides it by itself.
 */
#ifndef _DRM618_PREFIX_H_
#define _DRM618_PREFIX_H_

#include <sys/types.h>

/* types of Linux's asm/posix_types.h and linux/types.h (uapi/drm/drm.h, drm_file.h) */
typedef size_t __kernel_size_t;
typedef unsigned int __poll_t;

/* linux/compiler_attributes.h (drm_connector.h) */
#ifndef __nonstring
#define	__nonstring	__attribute__((__nonstring__))
#endif

/* linux/kconfig.h: no tristate options are modules here (drm_dp_helper.h) */
#ifndef IS_MODULE
#define	IS_MODULE(option)	(0)
#endif

#include <linux/kernel.h>
#include <linux/instruction_pointer.h>	/* kernel.h upstream: _THIS_IP_ (drm_print.h) */
#include <linux/err.h>			/* ERR_PTR, IS_ERR (many) */
#include <linux/mutex.h>
#include <linux/workqueue.h>		/* struct work_struct (drm_dp_helper.h) */
#include <linux/completion.h>		/* struct completion (drm_atomic.h) */
#include <linux/wait.h>			/* wait_queue_head_t (drm_file.h, drm_vblank.h) */
#include <linux/idr.h>
#include <linux/llist.h>
#include <linux/string_choices.h>

#endif
