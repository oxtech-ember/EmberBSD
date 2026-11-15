/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/i915_irq.c (irq_handler_t as the
 * type of intel_irq_handler's result, the handlers' signature irqreturn_t (int, void *),
 * the calls of request_irq and free_irq); the GPL original was not read.
 *
 * Shadow of the shared <linux/interrupt.h> for the 6.18 sources: a handler of Linux
 * takes the interrupt number and the cookie; the one of the shared layer (the 5.6
 * drivers) takes the cookie only.  request_irq and free_irq are declared, not
 * defined: the interrupt of the device is connected with the autoconfiguration.
 */
#ifndef _DRM618_LINUX_INTERRUPT_H_
#define _DRM618_LINUX_INTERRUPT_H_

#define	irq_handler_t	drm618_shared_irq_handler_t
#include_next <linux/interrupt.h>
#undef	irq_handler_t

typedef irqreturn_t (*irq_handler_t)(int, void *);

int	request_irq(unsigned int, irq_handler_t, unsigned long, const char *, void *);
const void *free_irq(unsigned int, void *);

#endif
