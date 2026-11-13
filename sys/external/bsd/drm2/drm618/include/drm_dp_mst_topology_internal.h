/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Origin: EmberBSD.  Written against the uses in
 * the MIT file drm_dp_mst_topology.c only (the GPL-only upstream header of
 * this name was not read): declarations of what that file defines and
 * other files of the core call.
 */
#ifndef _DRM618_DP_MST_TOPOLOGY_INTERNAL_H_
#define _DRM618_DP_MST_TOPOLOGY_INTERNAL_H_


struct drm_dp_sideband_msg_req_body;
struct drm_dp_sideband_msg_tx;
struct drm_printer;

void drm_dp_encode_sideband_req(const struct drm_dp_sideband_msg_req_body *,
    struct drm_dp_sideband_msg_tx *);
int drm_dp_decode_sideband_req(const struct drm_dp_sideband_msg_tx *,
    struct drm_dp_sideband_msg_req_body *);
void drm_dp_dump_sideband_msg_req_body(const struct drm_dp_sideband_msg_req_body *,
    int indent, struct drm_printer *);

#endif
