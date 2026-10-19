/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/* Reuse actual adapter/provider/cache/sequencer and real native backing. */
#include "publish-native-fixture.h"
#include "publish-hvs-types.h"
#include "publish-pv-types.h"
#include "publish-hdmi-types.h"
#include "publish-types.h"
#include "publish-hvs-body.h"
#include "publish-pv-body.h"
#include "publish-hdmi-body.h"
#include "publish-body.h"

struct profile_case {
	enum bcmv3d_hvs_publish_window window;
	uint32_t offset, value, flip;
};

/* Independent literal profile; no production table or encoder computes it. */
static const struct profile_case profile[] = {
	{ BCMV3D_PUBLISH_HVS, 0x20, 0x802000f0, 0x40000 },
	{ BCMV3D_PUBLISH_PV0, 0, 0x00177000, 0x20 },
	{ BCMV3D_PUBLISH_PV0, 4, 0x20000002, 0x10 },
	{ BCMV3D_PUBLISH_PV0, 8, 0, 1 },
	{ BCMV3D_PUBLISH_PV0, 0xc, 0x0094002c, 1 },
	{ BCMV3D_PUBLISH_PV0, 0x10, 0x00580780, 1 },
	{ BCMV3D_PUBLISH_PV0, 0x14, 0x00240005, 1 },
	{ BCMV3D_PUBLISH_PV0, 0x18, 0x00040438, 1 },
	{ BCMV3D_PUBLISH_PV0, 0x24, 0, 0x80 },
	{ BCMV3D_PUBLISH_PV0, 0x34, 0x20, 4 },
	{ BCMV3D_PUBLISH_PV0, 0x94, 0x110, 0x10 },
	{ BCMV3D_PUBLISH_HDMI0, 0xec, 0x0058c780, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0xf0, 0x0094002c, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0xf4, 0x05040438, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0x100, 0x05040438, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0xf8, 0x24, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0x104, 0x044c0024, 1 },
	{ BCMV3D_PUBLISH_HD, 0x44, 0x80810000, 0x40000 },
	{ BCMV3D_PUBLISH_HDMI0, 0x18c, 0x200, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0x114, 0, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0x1e4, 0, 1 },
	{ BCMV3D_PUBLISH_HDMI0, 0x7c, 1, 0x20 },
	{ BCMV3D_PUBLISH_DVP0, 0xbc, 0, 2 },
	{ BCMV3D_PUBLISH_DVP0, 0xf0, 0, 4 },
	{ BCMV3D_PUBLISH_DVP0, 0xf4, 0x354021, 1 },
	{ BCMV3D_PUBLISH_CSC0, 0, 7, 0x40 },
	{ BCMV3D_PUBLISH_CSC0, 0x2c, 0, 0x40 },
	{ BCMV3D_PUBLISH_PHY0, 0x1c, 0x2036, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0x28, 9, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0x2c, 0x407, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0x44, 0, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0x54, 0, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0, 0x7f, 1 },
	{ BCMV3D_PUBLISH_PHY0, 4, 0x1cf, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0x190, 1, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0x194, 1, 1 },
	{ BCMV3D_PUBLISH_RM0, 0x18, 0x98100000, 1 },
	{ BCMV3D_PUBLISH_CSC0, 4, 0x2000, 1 },
	{ BCMV3D_PUBLISH_CSC0, 8, 0, 1 },
	{ BCMV3D_PUBLISH_CSC0, 0xc, 0x20000000, 1 },
	{ BCMV3D_PUBLISH_CSC0, 0x10, 0, 1 },
	{ BCMV3D_PUBLISH_CSC0, 0x14, 0, 1 },
	{ BCMV3D_PUBLISH_CSC0, 0x18, 0x2000, 1 }
};

static struct {
	uint32_t regs[BCMV3D_PUBLISH_WINDOWS][0x4200 / 4];
	uint32_t saved[BCMV3D_PUBLISH_WINDOWS][0x4200 / 4];
	uint32_t ctrl, stat, stride, lptr, cob, dl, pri;
	unsigned int accesses, reads, writes, barriers, delays;
	unsigned int fail_read, fail_write, ignore_write, latch_delay;
	unsigned int dlist_writes, dlist_reads, reset_writes, channel_writes;
	bool pending, faulted, partial, timeout, reenter, eof;
	enum bcmv3d_hvs_publish_window pending_window;
	struct bcmv3d_hvs_publish_status status;
	struct bcm2712_hvs_frame frame;
} pb;

static const struct bcmv3d_hvs_publish_io publish_io;

static uint32_t
plane_word(unsigned int index)
{
	static const uint32_t literal[] = {
		0x4900c007, 0, 0x4000fff0, 0x0437077f, 0xc0c0c0c0,
		0, 0, 0x1e00, 0x80000000, 0x80000000
	};

	if (index == 0 && pb.frame.variant == BCM2712_HVS_D0)
		return 0x490cc007;
	if (index == 2 && pb.frame.variant == BCM2712_HVS_D0)
		return 0x0000fff0;
	if (index == 5 && pb.frame.upm_slot == 5)
		return 0x00021400;
	return literal[index];
}

static bool
started(void)
{

	return (pb.regs[0][pb.ctrl / 4] & 0x80000000) != 0 &&
	    (pb.regs[1][0] & 1) != 0 && (pb.regs[1][1] & 1) != 0;
}

static int
publish_test_read(void *arg, enum bcmv3d_hvs_publish_window window,
    uint32_t offset, uint32_t *out)
{
	unsigned int before;
	uint32_t value;

	CHECK(arg == &pb && !pb.faulted && !pb.pending);
	CHECK(!owner->lock.held && !bcm2712_iommuc_mutex.held);
	CHECK(window < BCMV3D_PUBLISH_WINDOWS && offset < 0x4200 &&
	    (offset & 3) == 0);
	if (pb.accesses++ == 0)
		CHECK(window == 0 && offset == 0);
	if (pb.reenter) {
		pb.reenter = false;
		before = pb.accesses;
		CHECK(bcmv3d_hvs_publish_1080p60(owner, &publish_io, &pb,
		    &pb.frame, &pb.status) == EBUSY);
		CHECK(pb.accesses == before);
	}
	if (++pb.reads == pb.fail_read) {
		pb.faulted = true;
		return EIO;
	}
	value = pb.regs[window][offset / 4];
	if (window == 0 && offset >= 0x4080 && offset < 0x40a8) {
		CHECK(pb.dlist_writes == 10 && pb.channel_writes == 0);
		CHECK(offset == 0x4080 + pb.dlist_reads * 4);
		pb.dlist_reads++;
	}
	if (window == 0 && offset == pb.ctrl + 4 && pb.writes != 0)
		CHECK(pb.reset_writes == 1);
	if (window == 0 && started() && !pb.timeout &&
	    pb.delays >= pb.latch_delay) {
		if (offset == pb.stat)
			value = pb.eof ? 0x006000 : 0x004000;
		if (offset == pb.dl)
			value = 0xabcd0020;
	}
	*out = value;
	return 0;
}

static int
publish_test_write(void *arg, enum bcmv3d_hvs_publish_window window,
    uint32_t offset, uint32_t value)
{
	bool failed;

	CHECK(arg == &pb && !pb.faulted && !pb.pending);
	CHECK(window == BCMV3D_PUBLISH_HVS || window == BCMV3D_PUBLISH_PV0);
	CHECK(pb.status.native_valid && pb.status.native.backing.prepared);
	CHECK(pb.status.native.backing.sealed && pb.status.native.backing.syncs == 5);
	CHECK(pb.status.mutation_attempted);
	pb.accesses++;
	pb.writes++;
	failed = pb.writes == pb.fail_write;
	if (!failed || pb.partial) {
		if (window == 0 && offset >= 0x4080 && offset < 0x40a8) {
			CHECK(pb.channel_writes == 0 && !started());
			CHECK(offset == 0x4080 + pb.dlist_writes * 4);
			CHECK(value == plane_word(pb.dlist_writes));
			pb.dlist_writes++;
		} else if (window == 0 && offset == pb.lptr) {
			CHECK(pb.dlist_writes == 10 && pb.dlist_reads == 10);
			CHECK(pb.barriers == 10 && value == 32);
			CHECK(memcmp(pb.regs[0] + 0x1000, pb.saved[0] + 0x1000, 128) == 0);
			pb.channel_writes++;
		} else if (window == 0 && offset == pb.ctrl) {
			if (value == 0x40000000) {
				CHECK(pb.channel_writes == 1 && pb.barriers == 11);
				pb.reset_writes++;
				pb.regs[0][(pb.ctrl + 4) / 4] ^= 0x200;
			} else {
				CHECK(value == 0x877f0437 && pb.channel_writes == 3);
				CHECK(pb.regs[0][(pb.ctrl + 4) / 4] == 0xa55a0200);
			}
			pb.channel_writes++;
		} else if (window == 0 && offset == pb.ctrl + 4) {
			CHECK(pb.channel_writes == 2 && value == 0xa55a0200);
			pb.channel_writes++;
		} else if (window == 1 && offset == 0) {
			CHECK(pb.channel_writes == 4 && pb.barriers == 14);
			CHECK(value == (pb.saved[1][0] | 1U));
		} else {
			CHECK(window == 1 && offset == 4);
			CHECK(pb.barriers == 15 && (pb.regs[1][0] & 1) != 0);
			CHECK(value == (pb.saved[1][1] | 1U));
		}
		if (pb.writes != pb.ignore_write)
			pb.regs[window][offset / 4] = value;
	}
	if (failed) {
		pb.faulted = true;
		return EIO;
	}
	pb.pending = true;
	pb.pending_window = window;
	return 0;
}

static void
publish_test_barrier(void *arg, enum bcmv3d_hvs_publish_window window)
{

	CHECK(arg == &pb && !pb.faulted && pb.pending);
	CHECK(window == pb.pending_window);
	pb.pending = false;
	pb.barriers++;
}

static void
publish_test_delay(void *arg, unsigned int usec)
{

	CHECK(arg == &pb && !pb.faulted && !pb.pending);
	CHECK(started() && usec == 1000);
	pb.delays++;
}

static const struct bcmv3d_hvs_publish_io publish_io = {
	.read = publish_test_read,
	.write = publish_test_write,
	.barrier = publish_test_barrier,
	.delay = publish_test_delay
};

static void
publish_setup(unsigned int variant)
{
	unsigned int i;

	memset(&pb, 0, sizeof(pb));
	pb.frame.variant = variant == 0 ? BCM2712_HVS_C0 : BCM2712_HVS_D0;
	pb.frame.dma_span = 8294400;
	pb.frame.upm_length = 15616;
	pb.ctrl = variant == 0 ? 0x30 : 0x100;
	pb.stat = variant == 0 ? 0x44 : 0x118;
	pb.stride = variant == 0 ? 0x20 : 0x40;
	pb.lptr = variant == 0 ? 0x3c : 0x110;
	pb.cob = variant == 0 ? 0x40 : 0x114;
	pb.dl = variant == 0 ? 0x48 : 0x11c;
	pb.pri = variant == 0 ? 0xb8 : 0x38;
	pb.regs[0][0] = variant == 0 ? 0x13579b53 : 0x2468ac54;
	for (i = 0; i < __arraycount(profile); i++)
		pb.regs[profile[i].window][profile[i].offset / 4] = profile[i].value;
	for (i = 0; i < 3; i++)
		pb.regs[0][(pb.ctrl + i * pb.stride) / 4] = 0x40000000;
	pb.regs[0][(pb.ctrl + 4) / 4] = 0xa55a0101;
	pb.regs[0][pb.pri / 4] = 0xffffffff;
	pb.regs[0][pb.pri / 4 + 1] = 0xffffffff;
	pb.regs[0][pb.cob / 4] = 0x87004b10;
	pb.regs[0][(pb.cob + pb.stride) / 4] = 0x4b000f10;
	pb.regs[0][(pb.cob + 2 * pb.stride) / 4] = 0x0f000000;
	for (i = 0; i < 64; i++)
		pb.regs[0][0x4000 / 4 + i] = 0xabc00000 + i;
	memcpy(pb.saved, pb.regs, sizeof(pb.regs));
}

static void
native_gate(unsigned int which)
{
	struct bcmv3d_mmu4_native_status *n = &owner->status;

	switch (which) {
	case 0:
		n->stage = BCMV3D_MMU4_NATIVE_PREPARED;
		break;
	case 1:
		n->program_attempted = false;
		break;
	case 2:
		n->sealed = false;
		break;
	case 3:
		n->write_attempted = false;
		break;
	case 4:
		n->sequence_valid = false;
		break;
	case 5:
		n->shared_valid = false;
		break;
	case 6:
		n->icac_valid = false;
		break;
	case 7:
		n->backing_valid = false;
		break;
	case 8:
		n->sequence.prepared = false;
		break;
	case 9:
		n->sequence.retained = false;
		break;
	case 10:
		n->sequence.sequence_complete = false;
		break;
	case 11:
		n->shared.retained = 0;
		break;
	case 12:
		n->shared.reserved |= 1;
		break;
	case 13:
		n->shared.flush_complete = false;
		break;
	case 14:
		n->icac.mmu4.attempted = false;
		break;
	case 15:
		n->icac.core.retained = 0;
		break;
	case 16:
		n->icac.core.flush_complete = false;
		break;
	case 17:
		n->backing.prepared = false;
		break;
	case 18:
		n->backing.sealed = false;
		break;
	case 19:
		n->backing.mapping_uncertain = true;
		break;
	case 20:
		n->backing.first_error = EFAULT;
		break;
	case 21:
		n->backing.syncs = 4;
		break;
	case 22:
		n->backing.frame_bytes = 8294399;
		break;
	case 23:
		n->operation_error = EIO;
		break;
	case 24:
		n->status_error = EIO;
		break;
	case 25:
		n->close_error = EIO;
		break;
	case 26:
		n->icac.mmu4.fault = EIO;
		break;
	case 27:
		n->sequence.fault = EIO;
		break;
	case 28:
		n->shared.fault = EIO;
		break;
	case 29:
		n->icac.core.fault = EIO;
		break;
	default:
		CHECK(false);
	}
}

static void
publication_scenario(unsigned int variant, const char *kind, unsigned int id,
    unsigned int *read_count, unsigned int *write_count)
{
	struct bcmv3d_hvs_publish_status saved;
	struct bcmv3d_mmu4_backing_status actual;
	struct bcmv3d_hvs_publish_io invalid;
	unsigned int before, native_accesses;
	int error, expected = 0;

	*read_count = *write_count = 0;
	adapter_setup();
	make_owner();
	if (strcmp(kind, "real-unready") != 0) {
		CHECK(bcmv3d_mmu4_native_prepare(owner) == 0);
		if (strcmp(kind, "real-partial") == 0 ||
		    strcmp(kind, "real-combined") == 0)
			fail_pages = true;
		if (strcmp(kind, "real-combined") == 0)
			backing_snapshot_failure = 3;
		error = bcmv3d_mmu4_native_program(owner);
		CHECK(error == (fail_pages ? EFAULT : 0));
		if (strcmp(kind, "real-combined") == 0) {
			CHECK(owner->status.program_error == EFAULT);
			CHECK(owner->status.fault == EAGAIN);
			CHECK(!owner->status.backing_valid);
			CHECK(bcmv3d_mmu4_backing_status(owner->backing, &actual) == 0);
			CHECK(actual.sealed && !actual.prepared);
			CHECK(actual.first_error == EFAULT && actual.mapping_uncertain);
		} else
			backing_snapshot_check();
	}
	publish_setup(variant);
	if (strcmp(kind, "profile") == 0) {
		CHECK(id < __arraycount(profile));
		pb.regs[profile[id].window][profile[id].offset / 4] ^= profile[id].flip;
		expected = EOPNOTSUPP;
	} else if (strcmp(kind, "native") == 0) {
		native_gate(id);
		expected = EBUSY;
	} else if (strcmp(kind, "read") == 0) {
		pb.fail_read = id;
		expected = EIO;
	} else if (strcmp(kind, "write") == 0) {
		pb.fail_write = (id + 1) / 2;
		pb.partial = (id & 1) == 0;
		expected = EIO;
	} else if (strcmp(kind, "ignore") == 0) {
		pb.ignore_write = id;
		expected = EIO;
	} else if (strcmp(kind, "busy") == 0) {
		if (id < 6)
			pb.regs[0][((id & 1 ? pb.stat : pb.ctrl) +
			    (id / 2) * pb.stride) / 4] |= id & 1 ? 0x2000 : 0x80000000;
		else
			pb.regs[1 + (id - 6) / 2][(id - 6) & 1] |= 1;
		expected = EOPNOTSUPP;
	} else if (strcmp(kind, "firmware") == 0) {
		pb.regs[1][0] = 0x07174000;
		pb.regs[1][1] = 2;
	} else if (strcmp(kind, "vcontrol-high") == 0) {
		pb.regs[1][1] |= id == 0 ? 0x80000000 : 0x00800000;
		expected = EOPNOTSUPP;
	} else if (strcmp(kind, "pv-format") == 0) {
		pb.regs[1][0] |= id == 0 ? 0x00200000 : 4;
		expected = EOPNOTSUPP;
	} else if (strcmp(kind, "reserved") == 0) {
		pb.regs[4][0xec / 4] |= 0xc0000000;
		pb.regs[1][0] |= 0x80000000;
	} else if (strcmp(kind, "limited") == 0) {
		pb.regs[8][1] = 0x1b80;
		pb.regs[8][2] = 0x04000000;
		pb.regs[8][3] = 0x1b800000;
		pb.regs[8][4] = 0x04000000;
		pb.regs[8][6] = 0x04001b80;
	} else if (strcmp(kind, "upm") == 0) {
		pb.frame.upm_offset = 512;
		pb.frame.upm_slot = 5;
	} else if (strcmp(kind, "timeout") == 0) {
		pb.timeout = true;
		expected = ETIMEDOUT;
	} else if (strcmp(kind, "delay") == 0) {
		pb.latch_delay = 2;
	} else if (strcmp(kind, "eof") == 0) {
		pb.eof = true;
		expected = ETIMEDOUT;
	} else if (strcmp(kind, "reenter") == 0) {
		pb.reenter = true;
	} else if (strcmp(kind, "version") == 0) {
		pb.regs[0][0] = id == 0 ? 0x55 : variant == 0 ? 0x54 : 0x53;
		expected = id == 0 ? EOPNOTSUPP : EAGAIN;
	} else if (strcmp(kind, "real-unready") == 0) {
		expected = EBUSY;
	} else if (strcmp(kind, "real-partial") == 0 ||
	    strcmp(kind, "real-combined") == 0) {
		expected = EFAULT;
	} else if (strcmp(kind, "context") == 0) {
		hard_irq = true;
		expected = EWOULDBLOCK;
	}
	memcpy(pb.saved, pb.regs, sizeof(pb.regs));
	if (strcmp(kind, "invalid") == 0) {
		invalid = publish_io;
		saved = pb.status;
		switch (id) {
		case 0:
			pb.frame.dma_base = 0x12345000;
			break;
		case 1:
			pb.frame.dma_span++;
			break;
		case 2:
			pb.frame.upm_length = 15360;
			break;
		case 3:
			pb.frame.upm_slot = 32;
			break;
		case 4:
			pb.frame.variant = 0x55;
			break;
		case 5:
			invalid.read = NULL;
			break;
		case 6:
			invalid.write = NULL;
			break;
		case 7:
			invalid.barrier = NULL;
			break;
		case 8:
			invalid.delay = NULL;
			break;
		case 9:
			error = bcmv3d_hvs_publish_1080p60(NULL, &invalid, &pb,
			    &pb.frame, &pb.status);
			goto invalid_result;
		case 10:
			error = bcmv3d_hvs_publish_1080p60(owner, NULL, &pb,
			    &pb.frame, &pb.status);
			goto invalid_result;
		case 11:
			error = bcmv3d_hvs_publish_1080p60(owner, &invalid, &pb,
			    NULL, &pb.status);
			goto invalid_result;
		case 12:
			error = bcmv3d_hvs_publish_1080p60(owner, &invalid, &pb,
			    &pb.frame, NULL);
			goto invalid_result;
		default:
			CHECK(false);
		}
		error = bcmv3d_hvs_publish_1080p60(owner, &invalid, &pb,
		    &pb.frame, &pb.status);
invalid_result:
		CHECK(error == (id == 4 ? EOPNOTSUPP : EINVAL));
		CHECK(memcmp(&saved, &pb.status, sizeof(saved)) == 0);
		CHECK(pb.accesses == 0);
		return;
	}
	native_accesses = local_reads + local_writes + local_maps + calls + map_calls;
	error = bcmv3d_hvs_publish_1080p60(owner, &publish_io, &pb,
	    &pb.frame, &pb.status);
	hard_irq = false;
	CHECK(error == expected);
	CHECK(local_reads + local_writes + local_maps + calls + map_calls ==
	    native_accesses);
	CHECK(pb.status.attempted && pb.status.complete == (error == 0));
	CHECK(pb.status.first_error == expected);
	if (error == 0) {
		CHECK(pb.writes == 16 && pb.barriers == 16 && !pb.pending);
		CHECK(pb.status.native.backing.prepared && pb.status.native.backing.sealed);
		CHECK(pb.status.native.backing.syncs == 5);
		CHECK(pb.status.active_list == 32);
		CHECK((pb.status.channel_status & 0x6000) == 0x4000);
		CHECK(pb.regs[0][pb.ctrl / 4] == 0x877f0437);
		CHECK((pb.regs[1][0] & 1) != 0 && (pb.regs[1][1] & 1) != 0);
		CHECK(pb.delays == pb.latch_delay);
		if (strcmp(kind, "firmware") == 0) {
			CHECK(pb.regs[1][0] == 0x07174001);
			CHECK(pb.regs[1][1] == 3);
		}
	} else {
		CHECK(!pb.status.complete);
		if (strcmp(kind, "profile") == 0) {
			CHECK(pb.writes == 0 && pb.status.raw_valid);
			CHECK(pb.status.window == profile[id].window);
			CHECK(pb.status.offset == profile[id].offset);
			CHECK(pb.status.reject_mask != 0);
			CHECK((pb.status.raw & pb.status.reject_mask) != pb.status.reject_expected);
		}
		if (strcmp(kind, "native") == 0 || strcmp(kind, "real-unready") == 0 ||
		    strcmp(kind, "real-partial") == 0 ||
		    strcmp(kind, "real-combined") == 0 || strcmp(kind, "context") == 0)
			CHECK(pb.accesses == 0);
		if (strcmp(kind, "timeout") == 0 || strcmp(kind, "eof") == 0)
			CHECK(pb.delays == 100 && pb.status.polls == 100);
		if (strcmp(kind, "read") == 0)
			CHECK(!pb.status.raw_valid && pb.reads == pb.fail_read);
		if (strcmp(kind, "write") == 0)
			CHECK(pb.writes == pb.fail_write && pb.status.mutation_attempted);
	}
	CHECK(memcmp(pb.regs[0] + 0x1000, pb.saved[0] + 0x1000, 128) == 0);
	for (before = 2; before < BCMV3D_PUBLISH_WINDOWS; before++)
		CHECK(memcmp(pb.regs[before], pb.saved[before],
		    sizeof(pb.regs[before])) == 0);
	before = pb.accesses;
	CHECK(bcmv3d_hvs_publish_1080p60(owner, &publish_io, &pb,
	    &pb.frame, &pb.status) == (error != 0 ? error : EBUSY));
	CHECK(pb.accesses == before);
	CHECK(heap_frees == 0 && drm_puts == 0);
	*read_count = pb.reads;
	*write_count = pb.writes;
}

static unsigned int boots, total_checks;

static void
run_publication(unsigned int variant, const char *kind, unsigned int id,
    unsigned int *reads_out, unsigned int *writes_out)
{
	unsigned int result[3];
	int fd[2], status;
	pid_t child, waited;
	ssize_t bytes;
	const char *selection = getenv("HVS_PUBLISH_SELECT");
	char name[96];

	(void)snprintf(name, sizeof(name), "%u:%s:%u", variant, kind, id);
	if (selection != NULL && strcmp(selection, name) != 0)
		return;
	CHECK(pipe(fd) == 0);
	child = fork();
	CHECK(child >= 0);
	if (child == 0) {
		close(fd[0]);
		checks = 0;
		publication_scenario(variant, kind, id, &result[1], &result[2]);
		result[0] = checks;
		CHECK(write(fd[1], result, sizeof(result)) == sizeof(result));
		_exit(0);
	}
	close(fd[1]);
	do {
		bytes = read(fd[0], result, sizeof(result));
	} while (bytes < 0 && errno == EINTR);
	close(fd[0]);
	do {
		waited = waitpid(child, &status, 0);
	} while (waited < 0 && errno == EINTR);
	CHECK(waited == child);
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT) {
		raise(SIGABRT);
		_exit(70);
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0 ||
	    bytes != sizeof(result)) {
		fprintf(stderr, "HVS publish child %s failed\n", name);
		exit(70);
	}
	boots++;
	total_checks += result[0];
	*reads_out = result[1];
	*writes_out = result[2];
}

int
main(void)
{
	unsigned int variant, i, nreads = 100, nwrites = 16, r = 0, w = 0;
	static const char *const single[] = {
		"limited", "upm", "reserved", "timeout", "delay", "eof", "reenter",
		"real-unready", "real-partial", "real-combined", "context", "firmware"
	};

	for (variant = 0; variant < 2; variant++) {
		run_publication(variant, "positive", 0, &r, &w);
		if (getenv("HVS_PUBLISH_SELECT") == NULL) {
			nreads = r;
			nwrites = w;
		}
		for (i = 0; i < __arraycount(single); i++)
			run_publication(variant, single[i], 0, &r, &w);
		for (i = 0; i < 2; i++) {
			run_publication(variant, "version", i, &r, &w);
			run_publication(variant, "vcontrol-high", i, &r, &w);
			run_publication(variant, "pv-format", i, &r, &w);
		}
		for (i = 0; i < 13; i++)
			run_publication(variant, "invalid", i, &r, &w);
		for (i = 0; i < 10; i++)
			run_publication(variant, "busy", i, &r, &w);
		for (i = 0; i < __arraycount(profile); i++)
			run_publication(variant, "profile", i, &r, &w);
		for (i = 0; i < 30; i++)
			run_publication(variant, "native", i, &r, &w);
		for (i = 1; i <= nreads; i++)
			run_publication(variant, "read", i, &r, &w);
		for (i = 1; i <= nwrites * 2; i++)
			run_publication(variant, "write", i, &r, &w);
		run_publication(variant, "ignore", 4, &r, &w);
		run_publication(variant, "ignore", 14, &r, &w);
		run_publication(variant, "ignore", 15, &r, &w);
		run_publication(variant, "ignore", 16, &r, &w);
	}
	CHECK(boots != 0);
	printf("HVS/PV0 publish contract: %u isolated boots, %u checks PASS\n",
	    boots, total_checks + checks);
	return 0;
}
