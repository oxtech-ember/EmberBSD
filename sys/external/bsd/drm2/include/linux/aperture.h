/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD native. Interface of Linux 6.18 include/linux/aperture.h (MIT).  The layer hands
 * the firmware framebuffer over in its own attach code, so there is nothing
 * for these calls to evict: each reports success without doing anything.
 */

#ifndef _LINUX_APERTURE_H_
#define _LINUX_APERTURE_H_

#include <linux/types.h>

struct pci_dev;

static inline int
aperture_remove_conflicting_devices(uintmax_t base __unused,
    uintmax_t size __unused, const char *name __unused)
{

	return 0;
}

static inline int
aperture_remove_all_conflicting_devices(const char *name __unused)
{

	return 0;
}

static inline int
aperture_remove_conflicting_pci_devices(struct pci_dev *pdev __unused,
    const char *name __unused)
{

	return 0;
}

#endif	/* _LINUX_APERTURE_H_ */
