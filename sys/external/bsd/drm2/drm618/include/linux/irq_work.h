/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the use of irq_work_sync in
 * dist618/drm/i915/gt/intel_breadcrumbs.c; the GPL original was not read.
 *
 * Shadow of the shared <linux/irq_work.h> for the 6.18 sources: adds
 * irq_work_sync (wait until a queued or running callback has returned).  The
 * shared irq_work clears its pending flag before the callback runs and has no
 * running state, so the wait is not written: declared, not defined.
 */
#ifndef _DRM618_LINUX_IRQ_WORK_H_
#define _DRM618_LINUX_IRQ_WORK_H_

#include_next <linux/irq_work.h>

void	irq_work_sync(struct irq_work *);

#endif
