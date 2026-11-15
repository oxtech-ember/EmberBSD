/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Own implementation.  Sources read: how the MIT files of dist618/drm/i915/display (intel_hdcp.c,
 * intel_hdcp_shim.h, intel_hdcp_gsc*.c, intel_display_types.h, intel_display_core.h) and
 * include/drm/display/drm_hdcp.h use the interface: types, members, calls; drm2/include/drm/i915_mei_hdcp_interface.h
 * of this tree (public domain, the 5.6 form of the same interface); the GPL original was not read.
 *
 * The interface between the display driver and the firmware (MEI or GSC)
 * that does the HDCP 2.2 cryptography.  The messages of the protocol
 * (struct hdcp2_*) are in drm/display/drm_hdcp.h.
 *
 * The numbers of the enumerations are those the firmware interface of the
 * 5.6 header of this tree has, where it had them; the others (the DDI and
 * transcoder codes of newer platforms) are NOT checked against the firmware
 * specification: they matter only when HDCP is enabled with firmware, which
 * this build does not do (no MEI driver; the arbiter is never bound).
 */
#ifndef _DRM618_DRM_INTEL_I915_HDCP_INTERFACE_H_
#define _DRM618_DRM_INTEL_I915_HDCP_INTERFACE_H_

#include <linux/mutex.h>
#include <linux/types.h>

#include <drm/display/drm_hdcp.h>

struct device;

enum hdcp_port_type {
	HDCP_PORT_TYPE_INVALID		= 0,
	HDCP_PORT_TYPE_INTEGRATED	= 1,
	HDCP_PORT_TYPE_LSPCON		= 2,
	HDCP_PORT_TYPE_CPDP		= 3,
};

enum hdcp_wired_protocol {
	HDCP_PROTOCOL_INVALID,
	HDCP_PROTOCOL_HDMI,
	HDCP_PROTOCOL_DP,
};

enum hdcp_ddi {
	HDCP_DDI_INVALID_PORT	= 0x0,
	HDCP_DDI_B		= 1,
	HDCP_DDI_C		= 2,
	HDCP_DDI_D		= 3,
	HDCP_DDI_E		= 4,
	HDCP_DDI_F		= 5,
	HDCP_DDI_A		= 7,
	HDCP_DDI_RANGE_BEGIN	= HDCP_DDI_B,
	HDCP_DDI_RANGE_END	= HDCP_DDI_A,
};

enum hdcp_transcoder {
	HDCP_INVALID_TRANSCODER	= 0x00,
	HDCP_TRANSCODER_EDP	= 0x01,
	HDCP_TRANSCODER_DSI0	= 0x02,
	HDCP_TRANSCODER_DSI1	= 0x03,
	HDCP_TRANSCODER_A	= 0x10,
	HDCP_TRANSCODER_B	= 0x11,
	HDCP_TRANSCODER_C	= 0x12,
	HDCP_TRANSCODER_D	= 0x13,
};

/* the state of one port for the firmware calls */
struct hdcp_port_data {
	enum hdcp_ddi hdcp_ddi;
	enum hdcp_transcoder hdcp_transcoder;
	u8 port_type;
	u8 protocol;
	u16 k;
	u32 seq_num_m;
	struct hdcp2_streamid_type *streams;
};

/* what the firmware driver offers */
struct i915_hdcp_ops {
	int (*initiate_hdcp2_session)(struct device *dev,
	    struct hdcp_port_data *data, struct hdcp2_ake_init *ake_data);
	int (*verify_receiver_cert_prepare_km)(struct device *dev,
	    struct hdcp_port_data *data, struct hdcp2_ake_send_cert *rx_cert,
	    bool *km_stored, struct hdcp2_ake_no_stored_km *ek_pub_km,
	    size_t *msg_sz);
	int (*verify_hprime)(struct device *dev, struct hdcp_port_data *data,
	    struct hdcp2_ake_send_hprime *rx_hprime);
	int (*store_pairing_info)(struct device *dev,
	    struct hdcp_port_data *data,
	    struct hdcp2_ake_send_pairing_info *pairing_info);
	int (*initiate_locality_check)(struct device *dev,
	    struct hdcp_port_data *data, struct hdcp2_lc_init *lc_init_data);
	int (*verify_lprime)(struct device *dev, struct hdcp_port_data *data,
	    struct hdcp2_lc_send_lprime *rx_lprime);
	int (*get_session_key)(struct device *dev, struct hdcp_port_data *data,
	    struct hdcp2_ske_send_eks *ske_data);
	int (*repeater_check_flow_prepare_ack)(struct device *dev,
	    struct hdcp_port_data *data,
	    struct hdcp2_rep_send_receiverid_list *rep_topology,
	    struct hdcp2_rep_send_ack *rep_send_ack);
	int (*verify_mprime)(struct device *dev, struct hdcp_port_data *data,
	    struct hdcp2_rep_stream_ready *stream_ready);
	int (*enable_hdcp_authentication)(struct device *dev,
	    struct hdcp_port_data *data);
	int (*close_hdcp_session)(struct device *dev,
	    struct hdcp_port_data *data);
};

/* what the display driver gets when the firmware driver binds */
struct i915_hdcp_arbiter {
	struct device *hdcp_dev;
	const struct i915_hdcp_ops *ops;
	/* Mutex to protect the above members. */
	struct mutex mutex;
};

#endif
