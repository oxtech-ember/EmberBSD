/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Origin: EmberBSD, AI-assisted. TV0 to DRM vblank correlation. */
#ifndef _SUN60I_A733_VBLANK_HISTORY_H
#define _SUN60I_A733_VBLANK_HISTORY_H

#define A733_VBLANK_HISTORY_SIZE 16

struct a733_vblank_stamp {
	uint64_t raw, sequence, time_ns;
};

/* Caller holds the private IPL_VM spin lock. Epoch zero is inactive. */
struct a733_vblank_history {
	struct a733_vblank_stamp stamps[A733_VBLANK_HISTORY_SIZE];
	uint64_t generation, seen, floor;
	bool active;
};

static inline void
a733_history_reset(struct a733_vblank_history *h, bool active)
{
	if (h->generation == UINT64_MAX) {
		h->active = false;
	} else {
		h->generation++;
		h->active = active;
	}
	h->seen = h->floor = 0;
}

static inline void
a733_history_record(struct a733_vblank_history *h, uint64_t generation,
    uint64_t raw, uint64_t prior, int64_t prior_ns, bool handled,
    uint64_t sequence, int64_t time_ns)
{
	uint64_t seen;

	if (!h->active || generation != h->generation)
		return;
	seen = h->seen;
	if (raw > seen)
		h->seen = raw;
	if (raw == 0 || (seen != 0 && (raw <= seen || raw - seen != 1)) ||
	    !handled || prior == UINT64_MAX || sequence != prior + 1 ||
	    time_ns <= 0 || time_ns <= prior_ns) {
		h->floor = 0;
		return;
	}
	if (h->floor == 0)
		h->floor = raw;
	h->stamps[raw % A733_VBLANK_HISTORY_SIZE] = (struct a733_vblank_stamp){
	    .raw = raw, .sequence = sequence, .time_ns = (uint64_t)time_ns
	};
}

static inline bool
a733_history_lookup(const struct a733_vblank_history *h, uint64_t raw,
    struct a733_vblank_stamp *out)
{
	const struct a733_vblank_stamp *stamp;

	if (!h->active || h->floor == 0 || raw < h->floor || raw > h->seen ||
	    h->seen - raw >= A733_VBLANK_HISTORY_SIZE)
		return false;
	stamp = &h->stamps[raw % A733_VBLANK_HISTORY_SIZE];
	if (stamp->raw != raw)
		return false;
	*out = *stamp;
	return true;
}

#endif
