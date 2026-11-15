/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.c of dist618 (MIT); the GPL
 * header of Linux was not read.
 *
 * Shadow of <linux/vgaarb.h> for the 6.18 core: the shared layer's, and the
 * claim of the legacy VGA resources the virtio-gpu driver's init makes when
 * its PCI device is a VGA one.  There is no arbiter here: nothing else
 * contends for them, the claim is always granted and the release is a no-op.
 */

#ifndef _DRM618_LINUX_VGAARB_H_
#define _DRM618_LINUX_VGAARB_H_

#include_next <linux/vgaarb.h>

#define	VGA_RSRC_NONE		0x00
#define	VGA_RSRC_LEGACY_IO	0x01
#define	VGA_RSRC_LEGACY_MEM	0x02

static inline int
vga_get_interruptible(struct pci_dev *pdev __unused, unsigned int rsrc __unused)
{

	return 0;
}

static inline void
vga_put(struct pci_dev *pdev __unused, unsigned int rsrc __unused)
{
}

#endif	/* _DRM618_LINUX_VGAARB_H_ */
