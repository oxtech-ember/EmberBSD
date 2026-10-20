/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _BCMV3D_HVS_PUBLISH_H_
#define _BCMV3D_HVS_PUBLISH_H_

#include <arm/broadcom/bcm2712_display_hvsvar.h>
#include "bcmv3d_mmu4_native.h"

/* Closed canonical windows. HDMI, PHY, RM and CSC are read-only here. */
enum bcmv3d_hvs_publish_window {
	BCMV3D_PUBLISH_HVS,
	BCMV3D_PUBLISH_PV0,
	BCMV3D_PUBLISH_PV1,
	BCMV3D_PUBLISH_HD,
	BCMV3D_PUBLISH_HDMI0,
	BCMV3D_PUBLISH_DVP0,
	BCMV3D_PUBLISH_PHY0,
	BCMV3D_PUBLISH_RM0,
	BCMV3D_PUBLISH_CSC0,
	BCMV3D_PUBLISH_WINDOWS
};

struct bcmv3d_hvs_publish_io {
	int (*read)(void *, enum bcmv3d_hvs_publish_window, uint32_t,
	    uint32_t *);
	int (*write)(void *, enum bcmv3d_hvs_publish_window, uint32_t,
	    uint32_t);
	void (*barrier)(void *, enum bcmv3d_hvs_publish_window);
	void (*delay)(void *, unsigned int);
};

enum bcmv3d_hvs_publish_stage {
	BCMV3D_PUBLISH_NATIVE,
	BCMV3D_PUBLISH_PROFILE,
	BCMV3D_PUBLISH_DLIST,
	BCMV3D_PUBLISH_CHANNEL,
	BCMV3D_PUBLISH_PV,
	BCMV3D_PUBLISH_LATCH,
	BCMV3D_PUBLISH_COMPLETE
};

/*
 * Read-only timing snapshot, in register order:
 * PV0 VCONTROL, VSYNCD_EVEN, HORZA/B, VERTA/B;
 * HDMI0 HORZA/B, VERTA0/1, VERTB0/1.
 * words counts only successful reads, PV0 first. A fault retains the prefix.
 */
struct bcmv3d_hvs_publish_timing {
	uint32_t pv[6], hdmi[6];
	unsigned int words;
};

#define BCMV3D_PUBLISH_LINK_WORDS	26U

/* Fresh zeroed storage in the retained boot owner. No release predicate. */
struct bcmv3d_hvs_publish_status {
	struct bcmv3d_mmu4_native_status native;
	struct bcm2712_hvs_frame frame;
	struct bcm2712_hvs_dlist list;
	struct bcmv3d_hvs_publish_timing timing;
	/* publish_link order, then CSC0 +4 through +18. Successful prefix only. */
	uint32_t link[BCMV3D_PUBLISH_LINK_WORDS];
	unsigned int link_words;
	enum bcmv3d_hvs_publish_stage stage;
	enum bcmv3d_hvs_publish_window window;
	uint32_t version, offset, raw, reject_mask, reject_expected;
	uint32_t active_list, channel_status;
	unsigned int reads_attempted, reads_completed;
	unsigned int writes_attempted, writes_completed, barriers, polls;
	int first_error;
	bool attempted, native_valid, raw_valid, mutation_attempted, complete;
};

/*
 * One fixed publication of the native owner's sealed, prepared initial frame.
 * The caller serializes access and holds normal-active fabric, real clocks,
 * canonical mappings, CPU/firmware handoff and exclusion of all other writers.
 * It owns the exact dlist SRAM words 32..41, supplied UPM range/slot and
 * inherited
 * COB partition before this call. Metadata, stopped channels and firmware ACK
 * do not prove a free pool, ownership, capacity or AXI drain. These conditions
 * and all native backing, mappings, clocks, callbacks and storage survive every
 * outcome until reboot. No retry, rollback, unmap, free or reconfiguration
 * exists.
 *
 * frame supplies the existing concrete UPM reservation. Its DMA address must
 * be the fixed MMU4 IOVA 0 and its span must be 8294400. No PA or V3D address
 * is
 * accepted. The helper queries actual native cached status and uses the actual
 * plane/PV/HDMI encoders. It does not create or program MMU4 or acquire an
 * owner.
 * It requires one conservative inherited 1080p60 RGB8/148.5 MHz digital
 * profile.
 * It writes only HVS and PV0. In particular, HDMI VID_CTL.ENABLE stays intact.
 *
 * NULL/invalid callbacks or invalid frame metadata return EINVAL before state
 * changes. Unsupported frame variants return EOPNOTSUPP. Then the attempt is
 * one-shot, including a native-state or profile refusal. First error is sticky.
 * Every I/O error stops later I/O. Observed value/mask/expected fields identify
 * a refused register profile. Completion records a latched list and running
 * channel, not visible pixels, physical timing/PLL lock, DRM/KMS or DMA drain.
 */
int bcmv3d_hvs_publish_1080p60(struct bcmv3d_mmu4_native *,
    const struct bcmv3d_hvs_publish_io *, void *,
    const struct bcm2712_hvs_frame *, struct bcmv3d_hvs_publish_status *);

#endif /* _BCMV3D_HVS_PUBLISH_H_ */
