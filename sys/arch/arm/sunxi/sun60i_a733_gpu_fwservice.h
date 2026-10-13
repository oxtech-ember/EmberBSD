/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
#ifndef _SUN60I_A733_GPU_FWSERVICE_H_
#define _SUN60I_A733_GPU_FWSERVICE_H_

#include <arm/sunxi/sun60i_a733_gpu_fwprobe.h>

struct sun60i_a733_gpu_fwobject {
	struct sun60i_a733_gpu_fwobject *next;
	struct sun60i_a733_gpu_dma dma;
	uint32_t fw_addr;
	size_t size;
	bool mapped;
};

struct sun60i_a733_gpu_fwservice {
	struct sun60i_a733_gpu_fwmem *memory;
	const struct sun60i_a733_gpu_fwboot_result *boot;
	struct sun60i_a733_gpu_fwqueue queue;
	struct sun60i_a733_gpu_fwobject *objects;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
};

/*
 * The platform owner serializes all calls with one adaptive mutex at IPL_NONE.
 * It keeps clocks, power, MMIO and firmware memory alive until system shutdown.
 * No reset or concurrent diagnostic command may use this firmware service.
 * Attach once, from a successful diagnostic command on the same live boot.
 * Allocation publishes new firmware mappings and waits for cache invalidation.
 * Allocation and free require a caller-owned pointer slot. Never pass the
 * service list head or an object's next pointer as that slot.
 * Every uncertain command failure freezes the service and retains all DMA.
 * These calls return positive native errno. They do not take ownership of
 * GPU page tables, GEM mappings, client contexts or their job completions.
 */
int sun60i_a733_gpu_fwservice_attach(struct sun60i_a733_gpu_fwservice *,
    bus_space_tag_t, bus_space_handle_t, struct sun60i_a733_gpu_fwmem *,
    const struct sun60i_a733_gpu_fwboot_result *,
    const struct sun60i_a733_gpu_fwprobe_result *);
int sun60i_a733_gpu_fwservice_alloc(struct sun60i_a733_gpu_fwservice *, size_t,
    struct sun60i_a733_gpu_fwobject **);
/*
 * Retire only after all references and jobs have completed. A context,
 * free list or render target first requires successful firmware CLEANUP.
 * Success clears *object. Failure retains it and its DMA, even if the
 * table entry was removed before a cache-invalidation failure.
 */
int sun60i_a733_gpu_fwservice_free(struct sun60i_a733_gpu_fwservice *,
    struct sun60i_a733_gpu_fwobject **);
int sun60i_a733_gpu_fwservice_flush(struct sun60i_a733_gpu_fwservice *);
int sun60i_a733_gpu_fwservice_command(struct sun60i_a733_gpu_fwservice *,
    const struct rogue_fwif_kccb_cmd *, struct sun60i_a733_gpu_fwprobe_result *);
int sun60i_a733_gpu_fwservice_observe(struct sun60i_a733_gpu_fwservice *,
    struct sun60i_a733_gpu_fwprobe_observation *);
/* Caller owns the object and validates its CPU/FW subranges. */
void sun60i_a733_gpu_fwservice_sync(struct sun60i_a733_gpu_fwservice *,
    struct sun60i_a733_gpu_fwobject *, int);

#endif /* _SUN60I_A733_GPU_FWSERVICE_H_ */
