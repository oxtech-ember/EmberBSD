# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# Compiler flags that every module of the Linux 6.18 DRM core shares
# (drmkms618, drmkms618_sched, drmkms618_ttm): the include path of the
# drmkms618 attribute (files.drmkms618) and the architecture defines.

# Our headers for 6.18 first, then Linux 6.18's, then the shared layer.
CPPFLAGS+=	-I${S}/external/bsd/drm2/drm618/include
# private headers of the files (drm_internal.h, drm_dp_mst_topology_internal.h, ...)
CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/drm
CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/drm/display
CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/drm/clients
CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/include
CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/include/uapi
CPPFLAGS+=	-I${S}/external/bsd/drm2/include
CPPFLAGS+=	-I${S}/external/bsd/common/include
CPPFLAGS+=	-D__KERNEL__
CPPFLAGS+=	-include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS+=	-DCONFIG_PCI=1
CPPFLAGS+=	-DDIAGNOSTIC
COPTS+=		-fwrapv
CWARNFLAGS+=	-Wno-missing-field-initializers

.if ${MACHINE_ARCH} == "i386"
CPPFLAGS+=	-DCONFIG_X86
CPPFLAGS+=	-DCONFIG_X86_PAT
.elif ${MACHINE_ARCH} == "x86_64"
CPPFLAGS+=	-DCONFIG_X86
CPPFLAGS+=	-DCONFIG_X86_64
CPPFLAGS+=	-DCONFIG_X86_PAT
.elif ${MACHINE_ARCH} == "aarch64"
CPPFLAGS+=	-DCONFIG_ARM64
.endif

