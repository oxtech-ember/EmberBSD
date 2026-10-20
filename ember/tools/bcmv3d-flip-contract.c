/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Real DRM consumers define Linux current. Keep its namespace intact. */
#define current (0)
#include "flip-actual.c"

static unsigned long checks;
#define CHECK(v) do { checks++; if (!(v)) { \
	fprintf(stderr, "CHECK %s:%d: %s\n", __FILE__, __LINE__, #v); \
	exit(1); } } while (0)

static void
start(struct bcmv3d_flip *s, uint32_t frame)
{
	struct bcmv3d_flip_event e = { .active_list = 32,
	    .frame_count = frame };

	memset(s, 0, sizeof(*s));
	CHECK(bcmv3d_flip_init(s, &e) == 0);
	CHECK(bcmv3d_flip_init(s, &e) == EBUSY);
	CHECK(s->current_slot == -1 && s->pending_slot == -1);
}

static struct bcmv3d_flip_update
observe(struct bcmv3d_flip *s, uint32_t head, unsigned int advance)
{
	struct bcmv3d_flip_update u = { 0 };
	struct bcmv3d_flip_event e = { .sequence = s->sequence + 1,
	    .active_list = head, .frame_count = (s->frame_count + advance) & 63 };

	CHECK(bcmv3d_flip_observe(s, &e, &u) == 0);
	return u;
}

static void
protocol(void)
{
	struct bcmv3d_flip s, copy;
	struct bcmv3d_flip_update u;
	struct bcmv3d_flip_event stale;
	unsigned int i, old = 0, slot, j;
	uint64_t epoch;

	start(&s, 63);
	CHECK(bcmv3d_flip_ready(&s, 0) == 0);
	CHECK(bcmv3d_flip_ready(&s, 0) == EBUSY);
	CHECK(bcmv3d_flip_submit(&s, 0) == 0);
	CHECK(bcmv3d_flip_submit(&s, 0) == EBUSY);
	u = observe(&s, 32, 0);
	CHECK(!u.latched && u.retired_mask == 0 && s.current_slot == -1);
	u = observe(&s, 42, 1);
	CHECK(u.latched && u.retired_mask == 0 && s.current_slot == 0);
	CHECK(s.frame_count == 0 && s.frame_epoch == 1);
	CHECK(bcmv3d_flip_cancel(&s, 0) == EBUSY);
	CHECK(bcmv3d_flip_claim_retired(&s, 0) == EBUSY);
/* A captured but delayed callback precedes this submission. */
CHECK(bcmv3d_flip_submit_after(&s, 0, s.sequence - 1) == EINVAL);
CHECK(s.pending_slot == -1);
CHECK(bcmv3d_flip_submit_after(&s, 0, s.sequence + 1) == 0);
u = observe(&s, 42, 1);
CHECK(!u.latched && s.pending_slot == 0);
u = observe(&s, 42, 1);
CHECK(u.latched && s.pending_slot == -1);
/* Same active BO is a new real flip event, not a new read lifetime. */
	CHECK(bcmv3d_flip_submit(&s, 0) == 0);
	CHECK(s.slot[0].state == BCMV3D_FLIP_CURRENT);
	u = observe(&s, 42, 1);
	CHECK(u.latched && u.retired_mask == 0 && s.current_slot == 0);
	for (i = 0; i < 4096; i++) {
		slot = (old + 1) % 3;
		CHECK(bcmv3d_flip_ready(&s, slot) == 0);
		CHECK(bcmv3d_flip_submit(&s, slot) == 0);
		CHECK(bcmv3d_flip_cancel(&s, slot) == EBUSY);
		u = observe(&s, bcmv3d_flip_head(old), 1);
		CHECK(!u.latched && u.retired_mask == 0);
		u = observe(&s, bcmv3d_flip_head(slot), 1);
		CHECK(u.latched && u.retired_mask == 0);
		CHECK(s.slot[old].state == BCMV3D_FLIP_RETIRING);
		CHECK(bcmv3d_flip_claim_retired(&s, old) == EBUSY);
		CHECK(bcmv3d_flip_ready(&s, old) == EBUSY);
		/* Repeated real IRQs with the same HVS count do not retire. */
		for (j = 0; j < 3; j++) {
			u = observe(&s, bcmv3d_flip_head(slot), 0);
			CHECK(!u.latched && u.retired_mask == 0);
		}
		u = observe(&s, bcmv3d_flip_head(slot), 1);
		CHECK(!u.latched && u.retired_mask == (1U << old));
		CHECK(bcmv3d_flip_claim_retired(&s, old) == 0);
		CHECK(s.slot[old].state == BCMV3D_FLIP_RECLAIMING);
		CHECK(bcmv3d_flip_ready(&s, old) == EBUSY);
		/* Native worker performs real map cleanup before this call. */
		CHECK(bcmv3d_flip_reclaimed(&s, old) == 0);
		CHECK(s.slot[old].state == BCMV3D_FLIP_FREE);
		old = slot;
	}
	epoch = s.frame_epoch;
	stale = (struct bcmv3d_flip_event) { .sequence = s.sequence - 1,
	    .active_list = 0xdead, .frame_count = 47 };
	u.latched = true; u.retired_mask = UINT32_MAX;
	CHECK(bcmv3d_flip_observe(&s, &stale, &u) == 0);
	CHECK(!u.latched && u.retired_mask == 0 && s.frame_epoch == epoch);
	copy = s;
	CHECK(bcmv3d_flip_submit(&copy, old) == EPERM);
	CHECK(bcmv3d_flip_submit(&s, BCMV3D_FLIP_BLANK) == 0);
	u = observe(&s, 72, 1); CHECK(u.latched && u.retired_mask == 0);
	u = observe(&s, 72, 1); CHECK(u.retired_mask == (1U << old));
	CHECK(bcmv3d_flip_claim_retired(&s, old) == 0);
	CHECK(bcmv3d_flip_reclaimed(&s, old) == 0);
	CHECK(bcmv3d_flip_ready(&s, old) == 0);
	CHECK(bcmv3d_flip_submit(&s, old) == 0);
	u = observe(&s, bcmv3d_flip_head(old), 1); CHECK(u.latched);
	u = observe(&s, bcmv3d_flip_head(old), 1);
	CHECK(u.retired_mask == (1U << BCMV3D_FLIP_BLANK));
	CHECK(bcmv3d_flip_claim_retired(&s, BCMV3D_FLIP_BLANK) == 0);
	CHECK(bcmv3d_flip_reclaimed(&s, BCMV3D_FLIP_BLANK) == 0);
	CHECK(s.slot[BCMV3D_FLIP_BLANK].state == BCMV3D_FLIP_READY);
}

static void
faults(void)
{
	struct bcmv3d_flip s;
	struct bcmv3d_flip_event e;
	struct bcmv3d_flip_update u;
	unsigned int i;
	int expected;

	for (i = 0; i < 7; i++) {
		start(&s, 0);
		CHECK(bcmv3d_flip_ready(&s, 0) == 0);
		CHECK(bcmv3d_flip_ready(&s, 1) == 0);
		CHECK(bcmv3d_flip_submit(&s, 0) == 0);
		u = observe(&s, 42, 1); CHECK(u.latched);
		CHECK(bcmv3d_flip_submit(&s, 1) == 0);
		u = observe(&s, 52, 1); CHECK(u.latched);
		e = (struct bcmv3d_flip_event) { .sequence = s.sequence + 1,
		    .active_list = 52, .frame_count = s.frame_count };
		expected = EOVERFLOW;
		switch (i) {
		case 0: e.sequence += 31; break;
		case 1: e.frame_count = (e.frame_count + 32) & 63; break;
		case 2: e.frame_count = 64; break;
		case 3: e.active_list = 42; expected = EPROTO; break;
		case 4: e.first_error = ENXIO; expected = ENXIO; break;
		case 5: e.first_error = -1; expected = EIO; break;
		case 6: s.frame_epoch = UINT64_MAX; e.frame_count++; break;
		}
		CHECK(bcmv3d_flip_observe(&s, &e, &u) == expected);
		CHECK(s.fault == expected);
		CHECK(bcmv3d_flip_claim_retired(&s, 0) == expected);
		CHECK(bcmv3d_flip_reclaimed(&s, 0) == expected);
		CHECK(bcmv3d_flip_ready(&s, 2) == expected);
		CHECK(bcmv3d_flip_submit(&s, 1) == expected);
		CHECK(bcmv3d_flip_fail(&s, ENOSPC) == expected);
	}
	start(&s, 0);
	CHECK(bcmv3d_flip_ready(&s, 0) == 0);
	CHECK(bcmv3d_flip_cancel(&s, 0) == 0);
	CHECK(bcmv3d_flip_ready(&s, 0) == 0);
	CHECK(bcmv3d_flip_reclaimed(&s, 0) == EBUSY);
	CHECK(bcmv3d_flip_head(4) == UINT32_MAX);
	CHECK(bcmv3d_flip_ready(&s, 3) == EINVAL);
	CHECK(bcmv3d_flip_submit(&s, 4) == EINVAL);
	CHECK(bcmv3d_flip_claim_retired(&s, 4) == EINVAL);
	CHECK(bcmv3d_flip_cancel(&s, 3) == EINVAL);
	CHECK(bcmv3d_flip_fail(&s, 0) == EINVAL);
}

int
main(void)
{

	protocol();
	faults();
	printf("PASS actual HVS flip protocol: %lu CHECKs,4096 switches,wrap,"
	    "same-BO,blank,stale callbacks and fault retention\n", checks);
	return 0;
}
