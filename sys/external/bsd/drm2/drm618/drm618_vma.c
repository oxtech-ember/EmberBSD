/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD; written to the interface in
 * include/drm618_mm.h, no Linux code.
 *
 * Not provided yet: every call says so on the console, once, and fails.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/atomic.h>
#include <sys/systm.h>

#include <drm618_mm.h>

static volatile unsigned said;

static void
unavailable(const char *fn)
{

	if (atomic_swap_uint(&said, 1) == 0)
		printf("drm618: user mappings of Linux files are not available: %s\n", fn);
}

int
drm618_vma_mmap(struct file *lf __unused, off_t *offp __unused,
    size_t len __unused, int prot __unused, int *flagsp __unused,
    int *advicep __unused, struct uvm_object **uobjp __unused,
    int *maxprotp __unused)
{

	unavailable(__func__);
	return EOPNOTSUPP;
}

vm_fault_t
vmf_insert_page(struct vm_area_struct *vma __unused, unsigned long addr __unused,
    struct page *page __unused)
{

	unavailable(__func__);
	return VM_FAULT_SIGBUS;
}

vm_fault_t
vmf_insert_pfn(struct vm_area_struct *vma __unused, unsigned long addr __unused,
    unsigned long pfn __unused)
{

	unavailable(__func__);
	return VM_FAULT_SIGBUS;
}

vm_fault_t
vmf_insert_pfn_prot(struct vm_area_struct *vma __unused,
    unsigned long addr __unused, unsigned long pfn __unused,
    pgprot_t prot __unused)
{

	unavailable(__func__);
	return VM_FAULT_SIGBUS;
}

int
remap_pfn_range(struct vm_area_struct *vma __unused, unsigned long addr __unused,
    unsigned long pfn __unused, unsigned long size __unused, pgprot_t prot __unused)
{

	unavailable(__func__);
	return -EOPNOTSUPP;
}

void
unmap_mapping_range(struct address_space *mapping __unused,
    loff_t holebegin __unused, loff_t holelen __unused, int even_cows __unused)
{

	unavailable(__func__);
}
