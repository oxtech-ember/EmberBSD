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

/*
 * The first entry of a table of ids (ended by an entry of zeros: no vendor, no
 * subvendor, no class mask) that matches the device: vendor, device, subsystem
 * vendor and device are equal or PCI_ANY_ID, the class agrees under the mask.
 */
static inline const struct pci_device_id *
pci_match_id(const struct pci_device_id *ids, struct pci_dev *pdev)
{

	if (ids == NULL)
		return NULL;
	for (; ids->vendor != 0 || ids->subvendor != 0 || ids->class_mask != 0;
	    ids++) {
		if ((ids->vendor == (uint32_t)PCI_ANY_ID ||
			ids->vendor == pdev->vendor) &&
		    (ids->device == (uint32_t)PCI_ANY_ID ||
			ids->device == pdev->device) &&
		    (ids->subvendor == (uint32_t)PCI_ANY_ID ||
			ids->subvendor == pdev->subsystem_vendor) &&
		    (ids->subdevice == (uint32_t)PCI_ANY_ID ||
			ids->subdevice == pdev->subsystem_device) &&
		    !((ids->class ^ pdev->class) & ids->class_mask))
			return ids;
	}
	return NULL;
}

/* Registers and bits of the PCI configuration header (the PCI specification). */
#ifndef PCI_COMMAND
#define	PCI_COMMAND		0x04
#define	PCI_COMMAND_IO		0x1
#define	PCI_COMMAND_MEMORY	0x2
#define	PCI_COMMAND_MASTER	0x4
#endif

#endif	/* _DRM618_LINUX_PCI_H_ */
