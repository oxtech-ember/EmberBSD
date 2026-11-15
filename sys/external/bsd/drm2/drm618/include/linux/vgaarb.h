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

#define	vga_client_register	drm618_shared_vga_client_register
#include_next <linux/vgaarb.h>
#undef	vga_client_register

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

/* the client of 6.18 gives the arbiter one function: the decode of the legacy resources (declared, not defined: no arbiter) */
int	vga_client_register(struct pci_dev *, unsigned int (*)(struct pci_dev *, bool));

#endif	/* _DRM618_LINUX_VGAARB_H_ */
