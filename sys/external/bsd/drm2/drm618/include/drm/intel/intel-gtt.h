/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the calls in dist618/drm/i915/gt/intel_ggtt_gmch.c (arguments and
 * results); the layer's drm2/include/drm/intel-gtt.h (BSD, the 5.6 form of the same interface); the GPL original was not read.
 *
 * The GMCH graphics translation table of the chips before Gen6 (the
 * bridge's aperture, filled through the AGP code).  Declared, not defined:
 * the platforms this interface serves need the NetBSD agp_i810 glue, which is
 * not part of the 6.18 layer yet (see the README, declared only).
 */
#ifndef _DRM618_DRM_INTEL_INTEL_GTT_H_
#define _DRM618_DRM_INTEL_INTEL_GTT_H_

#include <linux/agp_backend.h>
#include <linux/ioport.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

#ifndef AGP_USER_MEMORY
#define	AGP_USER_TYPES		(1 << 16)
#define	AGP_USER_MEMORY		(AGP_USER_TYPES)
#define	AGP_USER_CACHED_MEMORY	(AGP_USER_TYPES + 1)
#endif

struct agp_bridge_data;
struct pci_dev;

int	intel_gmch_probe(struct pci_dev *, struct pci_dev *,
	    struct agp_bridge_data *);
void	intel_gmch_remove(void);
bool	intel_gmch_enable_gtt(void);
void	intel_gmch_gtt_get(u64 *, phys_addr_t *, resource_size_t *);
void	intel_gmch_gtt_flush(void);
void	intel_gmch_gtt_insert_page(dma_addr_t, unsigned int, unsigned int);
void	intel_gmch_gtt_insert_sg_entries(struct sg_table *, unsigned int,
	    unsigned int);
void	intel_gmch_gtt_clear_range(unsigned int, unsigned int);
unsigned long intel_gmch_gtt_read_entry(unsigned int, bool *, bool *);

#endif
