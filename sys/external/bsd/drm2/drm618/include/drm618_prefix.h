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

/*
 * struct rb_node: NetBSD's <sys/rbtree.h> (also pulled in by the layer's idr.h
 * and ww_mutex.h) and the rb_node of the 6.18 sources, drm618/include/linux/
 * rbtree.h, are two different structures of the same size.  NetBSD's is
 * renamed while its header is read, its macros rb_left and rb_right (which
 * are member names in Linux) are dropped, and Linux's takes the name before
 * any other header can embed one: the layer headers that do keep the layout.
 */
#define	rb_node	netbsd_rb_node
#include <sys/rbtree.h>
#undef rb_node
#undef rb_left
#undef rb_right
/* <sys/tree.h> has RB_ROOT(head) (and is read once), Linux has RB_ROOT. */
#include <sys/tree.h>
#undef RB_ROOT
#include <linux/rbtree.h>

#include <sys/types.h>

/* types of Linux's asm/posix_types.h and linux/types.h (uapi/drm/drm.h, drm_file.h) */
typedef size_t __kernel_size_t;
typedef unsigned int __poll_t;

/* linux/compiler_attributes.h (drm_connector.h) */
#ifndef __nonstring
#define	__nonstring	__attribute__((__nonstring__))
#endif

/* linux/compiler_attributes.h (drm_managed.h) */
#ifndef __malloc
#define	__malloc	__attribute__((__malloc__))
#endif

/* linux/kconfig.h: no tristate options are modules here (drm_dp_helper.h) */
#ifndef IS_MODULE
#define	IS_MODULE(option)	(0)
#endif

#include <linux/kernel.h>
#include <linux/instruction_pointer.h>	/* kernel.h upstream: _THIS_IP_ (drm_print.h) */
#include <linux/err.h>			/* ERR_PTR, IS_ERR (many) */
#include <linux/module.h>		/* THIS_MODULE (drm_client, drm_gem fops) */
#include <linux/mutex.h>
#include <linux/workqueue.h>		/* struct work_struct (drm_dp_helper.h) */
#include <linux/completion.h>		/* struct completion (drm_atomic.h) */
#include <linux/wait.h>			/* wait_queue_head_t (drm_file.h, drm_vblank.h) */
#include <linux/idr.h>
#include <linux/llist.h>
#include <linux/string_choices.h>

/*
 * Structures the headers name in prototypes without declaring them (Linux's
 * include chains declare them on the way); needed as tags so that the
 * declarations are not local to the prototype (-Wvisibility is an error).
 */
struct poll_table_struct;	/* drm_file.h */
struct device_node;		/* drm_modes.h */
struct vm_area_struct;		/* drm_prime.h */

#include <drm618_layer.h>		/* list_last_entry_or_null, ALIGN(x, a), LIST_HEAD(name), ... */

#endif
