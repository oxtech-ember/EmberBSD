/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_FLIP_H_
#define _BCMV3D_FLIP_H_
#include <sys/types.h>

#define BCMV3D_FLIP_GEM_SLOTS 3U
#define BCMV3D_FLIP_BLANK 3U
#define BCMV3D_FLIP_SLOTS 4U

enum bcmv3d_flip_state {
	BCMV3D_FLIP_FREE,
	BCMV3D_FLIP_READY,
	BCMV3D_FLIP_PENDING,
	BCMV3D_FLIP_CURRENT,
	BCMV3D_FLIP_RETIRING,
	BCMV3D_FLIP_RETIRED,
	BCMV3D_FLIP_RECLAIMING
};

struct bcmv3d_flip_event {
	uint64_t sequence;
	uint32_t active_list, frame_count;
	int first_error;
};

struct bcmv3d_flip_slot {
	enum bcmv3d_flip_state state;
	uint64_t retire_after;
};

struct bcmv3d_flip {
	const struct bcmv3d_flip *owner;
	struct bcmv3d_flip_slot slot[BCMV3D_FLIP_SLOTS];
	uint64_t sequence, frame_epoch, queued_sequence, latch_sequence;
	uint32_t frame_count;
	int current_slot, pending_slot, fault;
	bool initialized;
};

struct bcmv3d_flip_update {
	uint32_t retired_mask;
	bool latched;
};

/*
 * Internal HVS frame protocol. It is not a DMA, mapping or fence owner.
 * Use fresh zeroed stable storage. Serialize all access. Positive BSD errno.
 * Initial snapshot comes from the retained real PV0 route with active head32.
 * Slots0..2 use heads42/52/62; slot3 is a permanently reserved empty head72.
 * ready follows actual map/read preparation. submit follows irreversible
 * consumer exposure and precedes the actual list-pointer publication under
 * the same native serialization. Neither operation establishes a latch.
 *
 * observe accepts only actual native VFP snapshots. A pending head must be
 * observed as active. The old head retires after subsequent HVS frame-count
 * progression. Six-bit deltas32..63 and IRQ-sequence gaps over31 are ambiguous
 * and fault. There is no elapsed-time, programmed-pointer or disabled-bit
 * retirement. An old callback cannot rewind state. Faults prohibit reclaim.
 * This follows the HVS frame protocol; it is not an independent AXI drain.
 *
 * Native code must retain the GEM/read lease, real DMA map and SRAM/UPM slot
 * until a process worker claims actual retirement and completes map cleanup.
 * reclaimed follows that cleanup. cancel is only for never-exposed READY.
 * No copy/reset/reinitialization or fault recovery is supported.
 */
int bcmv3d_flip_init(struct bcmv3d_flip *, const struct bcmv3d_flip_event *);
int bcmv3d_flip_ready(struct bcmv3d_flip *, unsigned int);
int bcmv3d_flip_cancel(struct bcmv3d_flip *, unsigned int);
int bcmv3d_flip_submit(struct bcmv3d_flip *, unsigned int);
/* Reject delayed callbacks captured at or before this native sequence. */
int bcmv3d_flip_submit_after(struct bcmv3d_flip *, unsigned int, uint64_t);
int bcmv3d_flip_observe(struct bcmv3d_flip *,
    const struct bcmv3d_flip_event *, struct bcmv3d_flip_update *);
int bcmv3d_flip_claim_retired(struct bcmv3d_flip *, unsigned int);
int bcmv3d_flip_reclaimed(struct bcmv3d_flip *, unsigned int);
int bcmv3d_flip_fail(struct bcmv3d_flip *, int);
uint32_t bcmv3d_flip_head(unsigned int);
#endif
