/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
/*
 * Built only when a kernel configuration selects both DRM cores:
 *
 *	file external/bsd/drm2/drm618/drm618_exclusive.c  drmkms618 & drmkms
 *
 * The Linux 5.6 core (drmkms, dist) and the Linux 6.18 core (drmkms618,
 * dist618) export the same drm_*, drm_gem_*, drm_atomic_* symbols with
 * different layouts.  A kernel has one of them.  The shared Linux layer
 * (drmkms_linux) is used by both.
 */
#ifdef __NetBSD__
#error "drmkms and drmkms618 are selected together: a kernel uses either the Linux 5.6 DRM core or the Linux 6.18 one"
#endif
