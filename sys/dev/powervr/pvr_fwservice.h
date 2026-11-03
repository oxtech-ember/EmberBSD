/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. AI-assisted native implementation. */
/* Origin: EmberBSD A733 firmware service, factored to the common owner. */
#ifndef _PVR_FWSERVICE_H_
#define _PVR_FWSERVICE_H_

#include <dev/powervr/pvr_fwprobe.h>

struct pvr_fwobject {
	struct pvr_fwobject *next;
	struct pvr_dma dma;
	uint32_t fw_addr;
	size_t size;
	bool mapped;
};

struct pvr_fwservice {
	struct pvr_fwmem *memory;
	const struct pvr_fwboot_result *boot;
	struct pvr_fwqueue queue;
	struct pvr_fwobject *objects;
	bus_space_tag_t bst;
	bus_space_handle_t bsh;
	void *event_cookie;
	int (*grow_prepare)(void *, uint32_t, struct rogue_fwif_freelist_gs_data *);
	int (*grow_complete)(void *, uint32_t, int);
	bool events_set, dispatching;
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
int pvr_fwservice_attach(struct pvr_fwservice *,
    bus_space_tag_t, bus_space_handle_t, struct pvr_fwmem *,
    const struct pvr_fwboot_result *,
    const struct pvr_fwprobe_result *);
/*
 * Install once under both device and platform gates. Keep cookie alive until
 * reboot. Callbacks run under both gates after the FWCCB read is confirmed.
 * They must not acquire either gate or call backend transport operations.
 * Complete may call fail(), which only marks state and performs no transport.
 * Every successful prepare receives one completion, including delta0/error.
 * Unknown events, handler errors and more than one ring of events per call
 * freeze the service. Each response has the normal bounded KCCB deadline.
 */
int pvr_fwservice_set_events(struct pvr_fwservice *,
    void *, int (*)(void *, uint32_t, struct rogue_fwif_freelist_gs_data *),
    int (*)(void *, uint32_t, int));
int pvr_fwservice_alloc(struct pvr_fwservice *, size_t,
    struct pvr_fwobject **);
/*
 * Retire only after all references and jobs have completed. A context,
 * free list or render target first requires successful firmware CLEANUP.
 * Success clears *object. Failure retains it and its DMA, even if the
 * table entry was removed before a cache-invalidation failure.
 */
int pvr_fwservice_free(struct pvr_fwservice *,
    struct pvr_fwobject **);
int pvr_fwservice_flush(struct pvr_fwservice *);
int pvr_fwservice_command(struct pvr_fwservice *,
    const struct rogue_fwif_kccb_cmd *, struct pvr_fwprobe_result *);
int pvr_fwservice_observe(struct pvr_fwservice *,
    struct pvr_fwprobe_observation *);
/* Caller owns the object and validates its CPU/FW subranges. */
void pvr_fwservice_sync(struct pvr_fwservice *,
    struct pvr_fwobject *, int);

#endif /* _PVR_FWSERVICE_H_ */
