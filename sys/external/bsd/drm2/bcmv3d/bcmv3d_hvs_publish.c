/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Register facts: Raspberry Pi Linux a1a601d4857978d810175365550c4536701df4ed,
 * vc4_regs.h, vc4_crtc.c, vc4_hvs.c, vc4_hdmi.c and vc4_hdmi_phy.c.
 * Independent BSD mechanism. No GPL implementation is copied here.
 */
#include <sys/param.h>
#include <sys/errno.h>
#include <arm/broadcom/bcm2712_display_pv0var.h>
#include <arm/broadcom/bcm2712_display_hdmi0var.h>
#include "bcmv3d_hvs_publish.h"

#define PUBLISH_FRAME_BYTES	8294400U
#define PUBLISH_DLIST		0x4080U
#define PUBLISH_HEAD		32U
#define PUBLISH_POLLS		100U
#define PUBLISH_MMU4_MEMBER	2U

struct publish_profile_word {
	enum bcmv3d_hvs_publish_window window;
	uint32_t offset, mask, expected;
};

/* Fixed read-only digital signature. These values do not prove PLL lock. */
static const struct publish_profile_word publish_link[] = {
	{ BCMV3D_PUBLISH_HD, 0x44, 0x99040000, 0x80000000 },
	{ BCMV3D_PUBLISH_HDMI0, 0x18c, 0x70f, 0x200 },
	{ BCMV3D_PUBLISH_HDMI0, 0x114, 0xf, 0 },
	{ BCMV3D_PUBLISH_HDMI0, 0x1e4, 1, 0 },
	{ BCMV3D_PUBLISH_HDMI0, 0x7c, 0x21, 1 },
	{ BCMV3D_PUBLISH_DVP0, 0xbc, 2, 0 },
	{ BCMV3D_PUBLISH_DVP0, 0xf0, 0xc, 0 },
	{ BCMV3D_PUBLISH_DVP0, 0xf4, 0xffffff, 0x354021 },
	{ BCMV3D_PUBLISH_CSC0, 0, 0x7f, 7 },
	{ BCMV3D_PUBLISH_CSC0, 0x2c, 0xc0, 0 },
	{ BCMV3D_PUBLISH_PHY0, 0x1c, 0x23ff, 0x2036 },
	{ BCMV3D_PUBLISH_PHY0, 0x28, 0x1f, 9 },
	{ BCMV3D_PUBLISH_PHY0, 0x2c, 0x7ff, 0x407 },
	{ BCMV3D_PUBLISH_PHY0, 0x44, 0xf, 0 },
	{ BCMV3D_PUBLISH_PHY0, 0x54, 3, 0 },
	{ BCMV3D_PUBLISH_PHY0, 0, 0x7f, 0x7f },
	{ BCMV3D_PUBLISH_PHY0, 4, 0x1cf, 0x1cf },
	{ BCMV3D_PUBLISH_PHY0, 0x190, 1, 1 },
	{ BCMV3D_PUBLISH_PHY0, 0x194, 1, 1 },
	{ BCMV3D_PUBLISH_RM0, 0x18, 0xffffffff, 0x98100000 }
};

static int
publish_native(const struct bcmv3d_mmu4_native_status *n)
{

	if (n->program_error != 0)
		return n->program_error;
	if (n->prepare_error != 0)
		return n->prepare_error;
	if (n->fault != 0)
		return n->fault;
	if (n->stage != BCMV3D_MMU4_NATIVE_COMPLETE ||
	    !n->prepare_attempted || !n->program_attempted || !n->mapped ||
	    !n->debug_valid || !n->backing_created || !n->sealed ||
	    !n->write_attempted || n->operation_error != 0 ||
	    n->close_error != 0 || n->status_error != 0 ||
	    !n->sequence_valid || !n->shared_valid || !n->icac_valid ||
	    !n->backing_valid || !n->sequence.prepared ||
	    !n->sequence.reservation_owned || !n->sequence.retained ||
	    !n->sequence.mutation_attempted || !n->sequence.sequence_complete ||
	    n->sequence.first_error != 0 || n->sequence.fault != 0 ||
	    n->sequence.close_error != 0 || n->shared.fault != 0 ||
	    n->shared.reserved != PUBLISH_MMU4_MEMBER ||
	    n->shared.retained != PUBLISH_MMU4_MEMBER ||
	    !n->shared.flush_complete || !n->icac.software_attached ||
	    !n->icac.core_initialized || !n->icac.mmu4.attempted ||
	    n->icac.mmu4.first_error != 0 || n->icac.mmu4.fault != 0 ||
	    n->icac.core.fault != 0 ||
	    n->icac.core.reserved != PUBLISH_MMU4_MEMBER ||
	    n->icac.core.retained != PUBLISH_MMU4_MEMBER ||
	    !n->icac.core.flush_complete || !n->backing.prepare_started ||
	    !n->backing.prepared || !n->backing.sealed ||
	    n->backing.mapping_uncertain || n->backing.first_error != 0 ||
	    n->backing.stage != BCMV3D_MMU4_BACKING_PREPARED ||
	    n->backing.syncs != 5 ||
	    n->backing.frame_bytes < PUBLISH_FRAME_BYTES)
		return EBUSY;
	return 0;
}

static int
publish_read(const struct bcmv3d_hvs_publish_io *io, void *arg,
    struct bcmv3d_hvs_publish_status *s, enum bcmv3d_hvs_publish_window window,
    uint32_t offset)
{
	uint32_t value;
	int error;

	s->window = window;
	s->offset = offset;
	s->raw_valid = false;
	s->reject_mask = s->reject_expected = 0;
	s->reads_attempted++;
	error = io->read(arg, window, offset, &value);
	if (error != 0)
		return error;
	s->reads_completed++;
	s->raw = value;
	s->raw_valid = true;
	return 0;
}

static int
publish_cached(struct bcmv3d_hvs_publish_status *s,
    enum bcmv3d_hvs_publish_window window, uint32_t offset, uint32_t value,
    uint32_t mask, uint32_t expected)
{

	s->window = window;
	s->offset = offset;
	s->raw = value;
	s->raw_valid = true;
	s->reject_mask = mask;
	s->reject_expected = expected;
	if ((value & mask) == expected)
		return 0;
	return s->mutation_attempted ? EIO : EOPNOTSUPP;
}

static int
publish_expect(const struct bcmv3d_hvs_publish_io *io, void *arg,
    struct bcmv3d_hvs_publish_status *s, enum bcmv3d_hvs_publish_window window,
    uint32_t offset, uint32_t mask, uint32_t expected)
{
	int error;

	error = publish_read(io, arg, s, window, offset);
	if (error != 0)
		return error;
	return publish_cached(s, window, offset, s->raw, mask, expected);
}

static int
publish_write(const struct bcmv3d_hvs_publish_io *io, void *arg,
    struct bcmv3d_hvs_publish_status *s, enum bcmv3d_hvs_publish_window window,
    uint32_t offset, uint32_t value)
{
	int error;

	s->window = window;
	s->offset = offset;
	s->raw_valid = false;
	s->reject_mask = s->reject_expected = 0;
	s->mutation_attempted = true;
	s->writes_attempted++;
	error = io->write(arg, window, offset, value);
	if (error != 0)
		return error;
	s->writes_completed++;
	io->barrier(arg, window);
	s->barriers++;
	return 0;
}

int
bcmv3d_hvs_publish_1080p60(struct bcmv3d_mmu4_native *native,
    const struct bcmv3d_hvs_publish_io *io, void *arg,
    const struct bcm2712_hvs_frame *frame, struct bcmv3d_hvs_publish_status *s)
{
	struct bcm2712_hvs_dlist list;
	struct bcm2712_pv0_timing pv;
	struct bcm2712_hdmi0_timing hdmi;
	const struct publish_profile_word *word;
	static const uint32_t pv_offsets[] = { 4, 8, 0xc, 0x10, 0x14, 0x18 };
	static const uint32_t hdmi_offsets[] = {
		0xec, 0xf0, 0xf4, 0x100, 0xf8, 0x104
	};
	uint32_t ctrl, stat, stride, lptr, cob, dl, pri, value, scale, offset;
	uint32_t horza, horzb;
	unsigned int i, poll;
	int error;

	if (native == NULL || io == NULL || frame == NULL || s == NULL ||
	    io->read == NULL || io->write == NULL || io->barrier == NULL ||
	    io->delay == NULL)
		return EINVAL;
	if (s->attempted || s->complete || s->first_error != 0)
		return s->first_error != 0 ? s->first_error : EBUSY;
	if (frame->dma_base != 0 || frame->dma_span != PUBLISH_FRAME_BYTES)
		return EINVAL;
	error = bcm2712_hvs_encode_1080p60(frame, &list);
	if (error != 0)
		return error;
	error = bcm2712_pv0_encode_1080p60(&pv);
	if (error != 0)
		return error;
	error = bcm2712_hdmi0_encode_1080p60(&hdmi);
	if (error != 0)
		return error;
	s->attempted = true;
	s->frame = *frame;
	s->list = list;
	s->stage = BCMV3D_PUBLISH_NATIVE;
	error = bcmv3d_mmu4_native_cached_status(native, &s->native);
	if (error != 0)
		goto done;
	s->native_valid = true;
	error = publish_native(&s->native);
	if (error != 0)
		goto done;
#define READ(w, o) do { \
	error = publish_read(io, arg, s, (w), (o)); \
	if (error != 0) goto done; \
} while (0)
#define EXPECT(w, o, m, e) do { \
	error = publish_expect(io, arg, s, (w), (o), (m), (e)); \
	if (error != 0) goto done; \
} while (0)
#define CACHED(w, o, v, m, e) do { \
	error = publish_cached(s, (w), (o), (v), (m), (e)); \
	if (error != 0) goto done; \
} while (0)
#define WRITE(w, o, v) do { \
	error = publish_write(io, arg, s, (w), (o), (v)); \
	if (error != 0) goto done; \
} while (0)
	s->stage = BCMV3D_PUBLISH_PROFILE;
	/* VERSION precedes every generation-dependent register access. */
	READ(BCMV3D_PUBLISH_HVS, 0);
	s->version = s->raw;
	s->reject_mask = 0xff;
	s->reject_expected = frame->variant;
	switch (s->version & 0xff) {
	case BCM2712_HVS_C0:
		ctrl = 0x30;
		stat = 0x44;
		stride = 0x20;
		lptr = 0x3c;
		cob = 0x40;
		dl = 0x48;
		pri = 0xb8;
		break;
	case BCM2712_HVS_D0:
		ctrl = 0x100;
		stat = 0x118;
		stride = 0x40;
		lptr = 0x110;
		cob = 0x114;
		dl = 0x11c;
		pri = 0x38;
		break;
	default:
		error = EOPNOTSUPP;
		goto done;
	}
	if ((s->version & 0xff) != (uint32_t)frame->variant) {
		error = EAGAIN;
		goto done;
	}
	for (i = 0; i < 3; i++) {
		/* RESET may remain set after the actual quiesce mechanism. */
		EXPECT(BCMV3D_PUBLISH_HVS, ctrl + i * stride, 0x80000000, 0);
		EXPECT(BCMV3D_PUBLISH_HVS, stat + i * stride, 0x6000, 0);
	}
	for (i = 0; i < 2; i++) {
		EXPECT(BCMV3D_PUBLISH_PV0 + i, 0, 1, 0);
		EXPECT(BCMV3D_PUBLISH_PV0 + i, 4, 1, 0);
	}
	/* Match inherited configuration. This does not reserve its storage. */
	EXPECT(BCMV3D_PUBLISH_HVS, 0x20, 0x807d30f0, 0x802000f0);
	EXPECT(BCMV3D_PUBLISH_HVS, pri, 0xffffffff, 0xffffffff);
	EXPECT(BCMV3D_PUBLISH_HVS, pri + 4, 0xffffffff, 0xffffffff);
	EXPECT(BCMV3D_PUBLISH_HVS, cob, 0xffffffff, 0x87004b10);
	EXPECT(BCMV3D_PUBLISH_HVS, cob + stride, 0xffffffff, 0x4b000f10);
	EXPECT(BCMV3D_PUBLISH_HVS, cob + 2 * stride, 0xffffffff, 0x0f000000);
	/* Capture all timing words before checking any member of the group. */
	for (i = 0; i < __arraycount(pv_offsets); i++) {
		READ(BCMV3D_PUBLISH_PV0, pv_offsets[i]);
		s->timing.pv[i] = s->raw;
		s->timing.words++;
	}
	for (i = 0; i < __arraycount(hdmi_offsets); i++) {
		READ(BCMV3D_PUBLISH_HDMI0, hdmi_offsets[i]);
		s->timing.hdmi[i] = s->raw;
		s->timing.words++;
	}
	/* Retain FIFO/start policy. Check only format, repetition and route. */
	EXPECT(BCMV3D_PUBLISH_PV0, 0, 0x00e0003d, pv.control & 0x00e0003d);
	/* Exact VCONTROL selects one complete horizontal packing profile. */
	if (s->timing.pv[0] == 2) {
		value = 2;
		horza = 0x004a0016;
		horzb = 0x002c03c0;
	} else {
		value = pv.v_control;
		horza = pv.horza;
		horzb = pv.horzb;
	}
	CACHED(BCMV3D_PUBLISH_PV0, 4, s->timing.pv[0], 0xffffffff, value);
	CACHED(BCMV3D_PUBLISH_PV0, 8, s->timing.pv[1], 0xffffffff, pv.vsyncd_even);
	CACHED(BCMV3D_PUBLISH_PV0, 0xc, s->timing.pv[2], 0xffffffff, horza);
	CACHED(BCMV3D_PUBLISH_PV0, 0x10, s->timing.pv[3], 0xffffffff, horzb);
	CACHED(BCMV3D_PUBLISH_PV0, 0x14, s->timing.pv[4], 0xffffffff, pv.verta);
	CACHED(BCMV3D_PUBLISH_PV0, 0x18, s->timing.pv[5], 0xffffffff, pv.vertb);
	EXPECT(BCMV3D_PUBLISH_PV0, 0x24, 0x3ff, 0);
	EXPECT(BCMV3D_PUBLISH_PV0, 0x34, 0x3c, pv.mux_cfg);
	EXPECT(BCMV3D_PUBLISH_PV0, 0x94, 0xff0, pv.pipe_init_ctrl & 0xff0);
	CACHED(BCMV3D_PUBLISH_HDMI0, 0xec, s->timing.hdmi[0],
	    0x1fffffff, hdmi.horza);
	CACHED(BCMV3D_PUBLISH_HDMI0, 0xf0, s->timing.hdmi[1],
	    0x07ff07ff, hdmi.horzb);
	CACHED(BCMV3D_PUBLISH_HDMI0, 0xf4, s->timing.hdmi[2],
	    0x1f7f1fff, hdmi.verta0);
	CACHED(BCMV3D_PUBLISH_HDMI0, 0x100, s->timing.hdmi[3],
	    0x1f7f1fff, hdmi.verta1);
	CACHED(BCMV3D_PUBLISH_HDMI0, 0xf8, s->timing.hdmi[4],
	    0x3fff01ff, hdmi.vertb0);
	CACHED(BCMV3D_PUBLISH_HDMI0, 0x104, s->timing.hdmi[5],
	    0x3fff01ff, hdmi.vertb1);
	for (i = 0; i < __arraycount(publish_link); i++) {
		word = &publish_link[i];
		EXPECT(word->window, word->offset, word->mask, word->expected);
	}
	/* Accept the pinned full or limited RGB matrix. No YCbCr conversion. */
	READ(BCMV3D_PUBLISH_CSC0, 4);
	if (s->raw == 0x00001b80) {
		scale = 0x1b80;
		offset = 0x04000000;
	} else {
		scale = 0x2000;
		offset = 0;
	}
	EXPECT(BCMV3D_PUBLISH_CSC0, 4, 0xffffffff, scale);
	EXPECT(BCMV3D_PUBLISH_CSC0, 8, 0xffffffff, offset);
	EXPECT(BCMV3D_PUBLISH_CSC0, 0xc, 0xffffffff, scale << 16);
	EXPECT(BCMV3D_PUBLISH_CSC0, 0x10, 0xffffffff, offset);
	EXPECT(BCMV3D_PUBLISH_CSC0, 0x14, 0xffffffff, 0);
	EXPECT(BCMV3D_PUBLISH_CSC0, 0x18, 0xffffffff, offset | scale);

	s->stage = BCMV3D_PUBLISH_DLIST;
	for (i = 0; i < BCM2712_HVS_DLIST_WORDS; i++)
		WRITE(BCMV3D_PUBLISH_HVS, PUBLISH_DLIST + 4 * i, list.words[i]);
	/* Verify only while inactive. HVS can mutate context after activation. */
	for (i = 0; i < BCM2712_HVS_DLIST_WORDS; i++)
		EXPECT(BCMV3D_PUBLISH_HVS, PUBLISH_DLIST + 4 * i,
		    0xffffffff, list.words[i]);
	s->stage = BCMV3D_PUBLISH_CHANNEL;
	WRITE(BCMV3D_PUBLISH_HVS, lptr, PUBLISH_HEAD);
	WRITE(BCMV3D_PUBLISH_HVS, ctrl, 0x40000000);
	READ(BCMV3D_PUBLISH_HVS, ctrl + 4);
	value = s->raw & ~0x101U;
	WRITE(BCMV3D_PUBLISH_HVS, ctrl + 4, value);
	WRITE(BCMV3D_PUBLISH_HVS, ctrl, 0x877f0437);
	EXPECT(BCMV3D_PUBLISH_HVS, ctrl, 0xffffffff, 0x877f0437);
	s->stage = BCMV3D_PUBLISH_PV;
	READ(BCMV3D_PUBLISH_PV0, 0);
	value = s->raw | 1U;
	WRITE(BCMV3D_PUBLISH_PV0, 0, value);
	EXPECT(BCMV3D_PUBLISH_PV0, 0, 1, 1);
	READ(BCMV3D_PUBLISH_PV0, 4);
	value = s->raw | 1U;
	WRITE(BCMV3D_PUBLISH_PV0, 4, value);
	EXPECT(BCMV3D_PUBLISH_PV0, 4, 1, 1);
	s->stage = BCMV3D_PUBLISH_LATCH;
	for (poll = 0;; poll++) {
		s->polls = poll;
		READ(BCMV3D_PUBLISH_HVS, stat);
		s->channel_status = s->raw;
		READ(BCMV3D_PUBLISH_HVS, dl);
		s->active_list = s->raw & 0xfff;
		if (s->active_list == PUBLISH_HEAD &&
		    (s->channel_status & 0x6000) == 0x4000)
			break;
		if (poll == PUBLISH_POLLS) {
			error = ETIMEDOUT;
			goto done;
		}
		io->delay(arg, 1000);
	}
	s->stage = BCMV3D_PUBLISH_COMPLETE;
	s->complete = true;
	error = 0;
done:
	if (error != 0 && s->first_error == 0)
		s->first_error = error;
	return error;
#undef READ
#undef EXPECT
#undef CACHED
#undef WRITE
}
