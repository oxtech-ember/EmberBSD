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

/*
 * The CPU and the device see the same memory on this machine (x86 DMA is
 * cache coherent): nothing to flush or invalidate for a table.  Another
 * architecture needs the cache operations of its bus_dma here.
 */
#if defined(__x86_64__) || defined(__i386__)
static inline void
dma_sync_sgtable_for_cpu(struct device *dev __unused,
    struct sg_table *sgt __unused, enum dma_data_direction dir __unused)
{
}

static inline void
dma_sync_sgtable_for_device(struct device *dev __unused,
    struct sg_table *sgt __unused, enum dma_data_direction dir __unused)
{
}
#else
#error "dma_sync_sgtable_*: no cache maintenance for this architecture"
#endif

/* The largest segment a device takes: no table is mapped for a device here, nothing to limit. */
static inline int
dma_set_max_seg_size(struct device *dev __unused, unsigned int size __unused)
{

	return 0;
}

/* 0, or a negative errno: -EOPNOTSUPP, a device here carries no DMA tag. */
int	dma_map_sgtable(struct device *, struct sg_table *,
	    enum dma_data_direction, unsigned long);
void	dma_unmap_sgtable(struct device *, struct sg_table *,
	    enum dma_data_direction, unsigned long);

#endif	/* _DRM618_LINUX_DMA_MAPPING_H_ */
