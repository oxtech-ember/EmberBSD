/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: the uses of seqcount_mutex_t, seqcount_mutex_init,
 * write_seqcount_invalidate and seqprop_sequence in dist618/drm/i915/gt; the GPL original was not read.
 *
 * A seqcount with an associated mutex.  The association only serves the
 * lock checks of Linux; here the type is the layer's seqcount.
 */
#ifndef _DRM618_LINUX_SEQLOCK_H_
#define _DRM618_LINUX_SEQLOCK_H_

#include_next <linux/seqlock.h>

typedef struct seqcount seqcount_mutex_t;

#define	seqcount_mutex_init(s, lock)	seqcount_init(s)

/* a writer that changed nothing readable still makes readers of the old value retry */
static inline void
write_seqcount_invalidate(struct seqcount *seqcount)
{

	membar_producer();
	seqcount->sqc_gen += 2;
}

static inline unsigned
seqprop_sequence(const struct seqcount *seqcount)
{

	return raw_read_seqcount(seqcount);
}

#endif
