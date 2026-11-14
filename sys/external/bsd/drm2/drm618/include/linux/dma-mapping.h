/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Shadow of <linux/dma-mapping.h> for the 6.18 core: the
 * calls on a table and a struct device that drm_prime.c of dist618 (MIT)
 * makes.  A struct device here is the opaque device_t of NetBSD: it has no
 * bus_dma tag to map with, so mapping a table through it is not provided and
 * says so (drm618_dma_buf.c), as does a table that was never mapped for the
 * unmap.
 */

#ifndef _DRM618_LINUX_DMA_MAPPING_H_
#define _DRM618_LINUX_DMA_MAPPING_H_

#include_next <linux/dma-mapping.h>

struct device;
struct sg_table;

/* No limit on the size of one mapping is known: 0, as Linux says for none. */
static inline size_t
dma_max_mapping_size(struct device *dev __unused)
{

	return 0;
}

/* 0, or a negative errno: -EOPNOTSUPP, a device here carries no DMA tag. */
int	dma_map_sgtable(struct device *, struct sg_table *,
	    enum dma_data_direction, unsigned long);
void	dma_unmap_sgtable(struct device *, struct sg_table *,
	    enum dma_data_direction, unsigned long);

#endif	/* _DRM618_LINUX_DMA_MAPPING_H_ */
