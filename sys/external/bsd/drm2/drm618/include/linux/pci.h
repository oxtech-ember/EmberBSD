/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: virtgpu_drv.c of dist618 (MIT); the GPL
 * header of Linux was not read.
 *
 * Shadow of <linux/pci.h> for the 6.18 core: the two calls of the virtio-gpu
 * driver's PCI quirk that the shared layer does not have.
 */

#ifndef _DRM618_LINUX_PCI_H_
#define _DRM618_LINUX_PCI_H_

#include_next <linux/pci.h>

#define	PCI_VENDOR_ID_REDHAT_QUMRANET	0x1af4

/* A VGA compatible display controller. */
static inline bool
pci_is_vga(struct pci_dev *pdev)
{

	return (pdev->class >> 8) == PCI_CLASS_DISPLAY_VGA;
}

/*
 * The registry of devices that have a struct pci_dev is not searched by id
 * here: there is none to find.  A virtio device is not one (its parent is the
 * virtio(4) device), so the VGA arbitration of the driver's init is skipped.
 */
static inline struct pci_dev *
pci_get_device(unsigned int vendor __unused, unsigned int device __unused,
    struct pci_dev *from __unused)
{

	return NULL;
}

#endif	/* _DRM618_LINUX_PCI_H_ */
