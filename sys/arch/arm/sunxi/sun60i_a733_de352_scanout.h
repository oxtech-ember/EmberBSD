/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. Native single-plane client contract. */
#ifndef _SUN60I_A733_DE352_SCANOUT_H
#define _SUN60I_A733_DE352_SCANOUT_H

#include <sys/bus.h>
#include <arm/sunxi/sun60i_a733_output.h>

struct sun60i_a733_de352_client;

struct sun60i_a733_de352_state {
	struct sun60i_a733_output_mode mode;
	bus_dma_tag_t dmat;
	uint64_t max_dma_address;
	uint64_t observed_sequence;
};

struct sun60i_a733_de352_frame {
	bus_dma_tag_t dmat;
	bus_dmamap_t map;
	size_t bytes;
	uint32_t width, height, pitch;
	void *cookie;
	int (*retain)(void *);
	void (*release)(void *);
};

struct sun60i_a733_de352_completion {
	/* Software observations, not an exact hardware frame counter/time. */
	uint64_t observed_sequence;
	uint64_t observed_time_ns;
	bool shadow_fast;
};

/*
 * The first profile accepts linear XRGB8888, offset zero, one loaded DMA
 * segment, the exact cached mode, and addresses below 4 GiB. The dmat is a
 * stable native subregion tag. It stays valid for the retained boot lifetime.
 * The caller must not substitute a parent tag or unload a retained map.
 * get_state performs no display I/O. Before successful explicit stage 1 it
 * returns ENXIO or EAGAIN. Failure leaves every output argument unchanged.
 */
/* Start retained discovery/scanout. EAGAIN means checked HPD-low only.
 * Retry with the same provider. No DE register or DMA write precedes a sink. */
int sun60i_a733_de352_start(int);
int sun60i_a733_de352_get_state(int, struct sun60i_a733_de352_state *);
int sun60i_a733_de352_acquire(int, const void *,
    struct sun60i_a733_de352_client **);
/* A release is allowed only before this client attempts its first DMA write. */
int sun60i_a733_de352_release(struct sun60i_a733_de352_client *);
/*
 * Install or remove (NULL) the TV0 vblank interrupt callback for an acquired
 * client. EOPNOTSUPP means polled observation only. Remove the callback
 * before its storage ends. Call without DRM or provider locks.
 */
int sun60i_a733_de352_set_vblank_handler(struct sun60i_a733_de352_client *,
    sun60i_a733_vblank_fn, void *);
/*
 * Call from a sleepable worker without DRM, reservation or provider locks.
 * Complete producer synchronization before this call. Under the reservation
 * lock, recheck writers and publish a pending READ scanout fence. Keep it
 * pending until release. Keep the frame immutable. A producer wait alone does
 * not exclude a later writer. Do not hold the reservation lock in this call.
 * retain/release callbacks run without the DE provider lock. A successful
 * retain pins the loaded map, backing storage and callback code. Before that
 * success, the caller owns cleanup. The provider calls release
 * only before any DMA attempt, or after a later accepted list and two new TV0
 * observations retire this frame. Active frames remain pinned.
 * This synchronous operation can sleep to prepare the queue. It returns
 * success after RCQ completion,
 * register verification and two distinct TV0 observations. Failure after a
 * pointer-write attempt retains both old and new frames and their queues.
 * It quarantines the client until reboot. No implicit reset or stop is made.
 */
int sun60i_a733_de352_submit(struct sun60i_a733_de352_client *,
    const struct sun60i_a733_de352_frame *,
    struct sun60i_a733_de352_completion *);

/*
 * Submit immutable storage with caller-enforced deferred reuse. A private
 * copy must never be exposed to userspace. An opted-in direct owner must
 * have completed producer synchronization, excluded CPU aliasing and
 * published a READ fence that remains pending through the release callback.
 * Successive eligible frames can complete presentation at the live TV0
 * witness before the displaced frame's DMA retirement. Only release permits
 * reuse. Other transitions and missing witnesses use the ordinary barrier.
 * Faults retain all attempted maps.
 */
int sun60i_a733_de352_submit_shadow(struct sun60i_a733_de352_client *,
    const struct sun60i_a733_de352_frame *,
    struct sun60i_a733_de352_completion *);

/*
 * Obtain a later stock-barrier observation after a successful active submit
 * if the consumer lost its timestamp sample. Retains all current DMA maps.
 */
int sun60i_a733_de352_observe_current(struct sun60i_a733_de352_client *,
    struct sun60i_a733_de352_completion *);

/* Sleepable HPD query. It does not retire a frame or latch a display fault. */
int sun60i_a733_de352_detect(struct sun60i_a733_de352_client *, bool *);

/* Retire pixel DMA through a disabled OVL and hardware background fill.
 * This operation can sleep. The timing engine stays active.
 * Failure retains every attempted list. */
int sun60i_a733_de352_disable_plane(struct sun60i_a733_de352_client *,
    struct sun60i_a733_de352_completion *);

/* Retire pixel DMA before stopping output. A later submit restarts timing.
 * This operation can sleep. Retain all native leases and queues.
 * An uncertain stop blocks restart. */
int sun60i_a733_de352_stop(struct sun60i_a733_de352_client *,
    struct sun60i_a733_de352_completion *);

#endif
