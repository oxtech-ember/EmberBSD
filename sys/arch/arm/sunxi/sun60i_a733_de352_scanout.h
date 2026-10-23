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
};

/*
 * The first profile accepts linear XRGB8888, offset zero, one loaded DMA
 * segment, the exact cached mode, and addresses below 4 GiB. The dmat is a
 * stable native subregion tag. It stays valid for the retained boot lifetime.
 * The caller must not substitute a parent tag or unload a retained map.
 * get_state performs no display I/O. Before successful explicit stage 1 it
 * returns ENXIO or EAGAIN. Failure leaves every output argument unchanged.
 */
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
 * This bounded synchronous operation returns success after RCQ completion,
 * register verification and two distinct TV0 observations. Failure after a
 * pointer-write attempt retains both old and new frames and their queues.
 * It quarantines the client until reboot. No implicit reset or stop is made.
 */
int sun60i_a733_de352_submit(struct sun60i_a733_de352_client *,
    const struct sun60i_a733_de352_frame *,
    struct sun60i_a733_de352_completion *);

/* Sleepable HPD query. It does not retire a frame or latch a display fault. */
int sun60i_a733_de352_detect(struct sun60i_a733_de352_client *, bool *);

#endif
