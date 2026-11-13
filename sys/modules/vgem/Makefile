# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# vgem: software DRM render device for dma_fence / dma_resv / dma-buf tests
# (ember/tests/drm2-vgem-client.c).  Not part of the default module set:
#	nbmake-<arch> -C sys/modules/vgem dependall                 5.6 dma_resv calls
#	nbmake-<arch> -C sys/modules/vgem VGEM_NEWAPI=1 dependall   6.18 dma_resv calls

.include "../Makefile.inc"
.include "../drmkms/Makefile.inc"

KMOD=	vgem

WARNS=	3

.PATH:	${S}/external/bsd/drm2/vgem
.PATH:	${S}/external/bsd/drm2/drm

SRCS+=	vgem.c
SRCS+=	drm_gem_shmem_helper.c

CPPFLAGS+=	-I${S}/sys/modules/drmkms

.if defined(VGEM_NEWAPI)
CPPFLAGS+=	-DVGEM_NEWAPI
.endif

# pmap_enter is only a weak alias on x86: the module loader does not resolve it
CPPFLAGS+=	-Dpmap_enter=pmap_enter_default

.include <bsd.kmodule.mk>
