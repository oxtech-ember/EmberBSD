/* SPDX-License-Identifier: BSD-2-Clause */
/* Actual PV0 event core. Only register services are controlled. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "vblank-types.h"
static unsigned checks;
#define CHECK(c)                                                              \
	do {                                                                  \
		checks++;                                                     \
		if (!(c)) {                                                   \
			fprintf(stderr, "CHECK: %s\n", #c);                   \
			exit(1);                                              \
		}                                                             \
	} while (0)
#include "vblank-body.h"
struct registers {
	unsigned calls, fail, writes, barriers;
	uint32_t variant, control, status, active, pv, video, enabled, pending;
	bool ignore_mask, ignore_arm;
};
static int
read_reg(
    void *arg, enum bcmv3d_vblank_window w, uint32_t offset, uint32_t *out)
{
	struct registers *r = arg;
	if (++r->calls == r->fail)
		return EFAULT;
	if (w == BCMV3D_VBLANK_PV0) {
		switch (offset) {
		case 0:
			*out = r->pv;
			return 0;
		case 4:
			*out = r->video;
			return 0;
		case 0x24:
			*out = r->enabled;
			return 0;
		case 0x28:
			*out = r->pending;
			return 0;
		default:
			CHECK(false);
		}
	} else {
		if (offset == 0)
			*out = r->variant;
		else if (offset == (r->variant == 0x53 ? 0x30U : 0x100U))
			*out = r->control;
		else if (offset == (r->variant == 0x53 ? 0x44U : 0x118U))
			*out = r->status;
		else if (offset == (r->variant == 0x53 ? 0x48U : 0x11cU))
			*out = r->active;
		else
			CHECK(false);
		return 0;
	}
	return EINVAL;
}
static int
write_reg(
    void *arg, enum bcmv3d_vblank_window w, uint32_t offset, uint32_t value)
{
	struct registers *r = arg;
	if (++r->calls == r->fail)
		return EFAULT;
	CHECK(w == BCMV3D_VBLANK_PV0);
	if (offset == 0x28) {
		CHECK(value == 0x80);
		r->pending &= ~value;
	} else {
		CHECK(offset == 0x24 &&
		      (value == 0 || (value == 0x80 && r->enabled == 0)));
		if (!(value == 0 ? r->ignore_mask : r->ignore_arm))
			r->enabled = value;
	}
	r->writes++;
	return 0;
}
static void
barrier_reg(void *arg, enum bcmv3d_vblank_window w)
{
	struct registers *r = arg;
	CHECK(w == BCMV3D_VBLANK_PV0);
	r->barriers++;
}
static const struct bcmv3d_vblank_io io = {read_reg, write_reg, barrier_reg};
static struct registers
initial(uint32_t variant)
{
	return (struct registers){.variant = variant,
	    .control = 0x877f0437,
	    .status = (63U << 16) | 0x4000,
	    .active = 32,
	    .pv = 1,
	    .video = 1,
	    .pending = 0x81};
}
static void
normal(uint32_t variant)
{
	struct registers r = initial(variant);
	struct bcmv3d_vblank s = {0}, copy;
	r.enabled =
	    0x85; /* An inherited supported mask must be disabled first. */
	bool handled = true;
	unsigned before;
	CHECK(bcmv3d_vblank_irq(&s, &handled) == EPERM && !handled &&
	      r.calls == 0);
	CHECK(bcmv3d_vblank_prepare(&s, &io, &r, variant) == 0);
	CHECK(r.calls == 10 && r.writes == 2 && r.barriers == 2 &&
	      r.pending == 1);
	CHECK(
	    s.prepared && !s.armed && s.sequence == 0 && s.frame_count == 63);
	CHECK(bcmv3d_vblank_irq(&s, &handled) == EBUSY && !handled &&
	      r.calls == 10);
	copy = s;
	CHECK(bcmv3d_vblank_arm(&copy) == EPERM && r.calls == 10);
	CHECK(bcmv3d_vblank_arm(&s) == 0 && r.enabled == 0x80 &&
	      r.barriers == 3);
	before = r.calls;
	CHECK(bcmv3d_vblank_arm(&s) == EBUSY && r.calls == before);
	CHECK(bcmv3d_vblank_irq(&s, &handled) == 0 && !handled &&
	      s.spurious == 1);
	CHECK(s.sequence == 0 && r.pending == 1);
	r.pending = 0x81;
	r.status = 0x4000;
	r.active = 42;
	CHECK(bcmv3d_vblank_irq(&s, &handled) == 0 && handled);
	CHECK(s.sequence == 1 && s.frame_count == 0 && s.active_list == 42);
	CHECK(r.pending == 1 && r.barriers == 4);
	/* No counter advance is invented for an absent source. */
	CHECK(bcmv3d_vblank_irq(&s, &handled) == 0 && !handled &&
	      s.sequence == 1);
}
static void
faults(uint32_t variant)
{
	struct registers r;
	struct bcmv3d_vblank s;
	unsigned phase, pos, limit, before;
	bool handled;
	int error;
	for (phase = 0; phase < 3; phase++) {
		limit = phase == 0 ? 10 : phase == 1 ? 2 : 5;
		for (pos = 1; pos <= limit; pos++) {
			r = initial(variant);
			memset(&s, 0, sizeof(s));
			if (phase > 0)
				CHECK(bcmv3d_vblank_prepare(
					  &s, &io, &r, variant) == 0);
			if (phase > 1)
				CHECK(bcmv3d_vblank_arm(&s) == 0);
			r.pending |= 0x80;
			r.fail = r.calls + pos;
			if (phase == 0)
				error = bcmv3d_vblank_prepare(
				    &s, &io, &r, variant);
			else if (phase == 1)
				error = bcmv3d_vblank_arm(&s);
			else
				error = bcmv3d_vblank_irq(&s, &handled);
			CHECK(error == EFAULT && s.fault == EFAULT &&
			      r.calls == r.fail);
			CHECK(s.sequence == 0);
			before = r.calls;
			CHECK(bcmv3d_vblank_arm(&s) == EFAULT &&
			      r.calls == before);
			CHECK(bcmv3d_vblank_irq(&s, &handled) == EFAULT &&
			      !handled && r.calls == before);
		}
	}
}
static void
profiles(uint32_t variant)
{
	unsigned which;
	for (which = 0; which < 6; which++) {
		struct registers r = initial(variant);
		struct bcmv3d_vblank s = {0};
		if (which == 0)
			r.control = 0;
		if (which == 1)
			r.status = 0;
		if (which == 2)
			r.active = 42;
		if (which == 3)
			r.pv = 0;
		if (which == 4)
			r.video = 0;
		if (which == 5)
			r.enabled = 0x400;
		CHECK(bcmv3d_vblank_prepare(&s, &io, &r, variant) ==
			  (which == 5 ? EOPNOTSUPP : EBUSY) &&
		      r.writes == 0);
	}
}
int
main(void)
{
	struct bcmv3d_vblank s = {0};
	struct registers r = initial(0x54);
	CHECK(bcmv3d_vblank_prepare(NULL, &io, &r, 0x54) == EINVAL);
	CHECK(bcmv3d_vblank_prepare(&s, NULL, &r, 0x54) == EINVAL);
	CHECK(bcmv3d_vblank_prepare(&s, &io, &r, 0x55) == EOPNOTSUPP &&
	      r.calls == 0);
	normal(0x53);
	normal(0x54);
	faults(0x53);
	faults(0x54);
	profiles(0x53);
	profiles(0x54);
	memset(&s, 0, sizeof(s));
	r = initial(0x54);
	r.enabled = 0x85;
	r.ignore_mask = true;
	CHECK(bcmv3d_vblank_prepare(&s, &io, &r, 0x54) == EIO && !s.prepared);
	CHECK(s.raw_valid && s.last_offset == 0x24 && s.last_raw == 0x85 &&
	      s.reject_expected == 0);
	memset(&s, 0, sizeof(s));
	r = initial(0x54);
	r.ignore_arm = true;
	CHECK(bcmv3d_vblank_prepare(&s, &io, &r, 0x54) == 0);
	CHECK(
	    bcmv3d_vblank_arm(&s) == EIO && s.fault == EIO && s.sequence == 0);

	printf("PASS actual PV0 core: %u checks, two HVS variants,34 I/O "
	       "fault positions\n",
	    checks);
	return 0;
}
