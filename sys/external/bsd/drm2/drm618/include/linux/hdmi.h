/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: dist618/drm/i915/display/intel_dp.c
 * (intel_dp_hdr_metadata_infoframe_sdp_unpack: the buffer is the payload after the
 * two bytes of the header, HDMI_DRM_INFOFRAME_SIZE long); the layer's own
 * hdmi_drm_infoframe_pack (drm2/linux/linux_hdmi.c: field offsets of the payload;
 * CTA-861-G table 42); the GPL original was not read.
 *
 * Shadow of the shared <linux/hdmi.h> for the 6.18 sources: the inverse of
 * hdmi_drm_infoframe_pack_only.  The ranges of the values are not checked.
 */
#ifndef _DRM618_LINUX_HDMI_H_
#define _DRM618_LINUX_HDMI_H_

#include_next <linux/hdmi.h>

#include <sys/endian.h>

/*
 * The payload of a Dynamic Range and Mastering infoframe (without its header)
 * into frame.  -EINVAL when the buffer is shorter than the payload.
 */
static inline int
hdmi_drm_infoframe_unpack_only(struct hdmi_drm_infoframe *frame,
    const void *buffer, size_t size)
{
	const uint8_t *p = buffer;
	unsigned i;
	int ret;

	if (size < HDMI_DRM_INFOFRAME_SIZE)
		return -EINVAL;
	ret = hdmi_drm_infoframe_init(frame);
	if (ret)
		return ret;
	frame->eotf = p[0] & 0x7;
	frame->metadata_type = p[1] & 0x7;
	for (i = 0; i < __arraycount(frame->display_primaries); i++) {
		frame->display_primaries[i].x = le16dec(&p[2 + 4*i]);
		frame->display_primaries[i].y = le16dec(&p[2 + 4*i + 2]);
	}
	frame->white_point.x = le16dec(&p[14]);
	frame->white_point.y = le16dec(&p[16]);
	frame->min_display_mastering_luminance = le16dec(&p[18]);
	frame->max_display_mastering_luminance = le16dec(&p[20]);
	frame->max_cll = le16dec(&p[22]);
	frame->max_fall = le16dec(&p[24]);
	return 0;
}

#endif
