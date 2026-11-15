/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of the names below in the MIT files of
 * dist618/drm/i915 (arguments, results); the GPL originals were not read.
 *
 * Small pieces of the Linux device interfaces that the i915 driver of 6.18 names and the
 * layer lacks.  What is declared and not defined stays so until the device is attached
 * (PCI, interrupts, the root port, CEC, D3cold): a module that calls it does not link.
 */
#ifndef _I915DRM618_DEV_H_
#define _I915DRM618_DEV_H_

#include <linux/dmi.h>
#include <linux/pci.h>

/* The product SKU of the SMBIOS: not a field the layer's dmi_check_system knows; it reports an unknown field and does not match. */
#ifndef DMI_PRODUCT_SKU
#define	DMI_PRODUCT_SKU		(DMI_STRING_MAX + 1)
#endif

/* The switch of a static key on (the layer's key is a bool). */
static inline void
static_branch_enable(bool *key)
{

	*key = true;
}

static inline void
static_branch_disable(bool *key)
{

	*key = false;
}

/* PCI (declared, not defined) */
struct pci_dev *pcie_find_root_port(struct pci_dev *);
void	pci_d3cold_disable(struct pci_dev *);
void	pci_d3cold_enable(struct pci_dev *);
struct pci_bus;
void	pci_assign_unassigned_bus_resources(struct pci_bus *);

/* CEC: the notifier of a connector (declared, not defined: there is no CEC) */
struct cec_notifier;
struct cec_connector_info;
struct cec_notifier *cec_notifier_conn_register(struct device *, const char *,
    const struct cec_connector_info *);
void	cec_notifier_conn_unregister(struct cec_notifier *);
void	cec_notifier_set_phys_addr(struct cec_notifier *, u16);
void	cec_notifier_phys_addr_invalidate(struct cec_notifier *);

#endif
