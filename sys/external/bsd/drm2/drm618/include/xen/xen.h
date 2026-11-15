/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.h of dist618 (MIT); the GPL
 * header of Linux was not read.
 *
 * <xen/xen.h> for the 6.18 core: the kernel does not run as a Xen guest, and
 * nothing here goes through the Xen DMA ops.
 */

#ifndef _DRM618_XEN_XEN_H_
#define _DRM618_XEN_XEN_H_

#define	xen_domain()	(0)
#define	xen_pv_domain()	(0)
#define	xen_hvm_domain()	(0)
#define	xen_pvh_domain()	(0)

#endif	/* _DRM618_XEN_XEN_H_ */
