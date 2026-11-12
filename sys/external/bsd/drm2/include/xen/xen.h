/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/xen/xen.h (GPL-2.0).  EmberBSD DRM code
 * never runs as a Xen guest from the layer's point of view, so every domain
 * test is false.
 */

#ifndef _XEN_XEN_H_
#define _XEN_XEN_H_

#define	xen_domain()		(0)
#define	xen_pv_domain()		(0)
#define	xen_hvm_domain()	(0)
#define	xen_initial_domain()	(0)

#endif	/* _XEN_XEN_H_ */
