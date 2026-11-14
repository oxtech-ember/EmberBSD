# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# vgem on the Linux 6.18 DRM core: the same source as sys/modules/vgem, built
# for the headers of drmkms618 (VGEM_CORE618) and the 6.18 dma_resv calls.
# Needs the modules of the 6.18 core (configuration GENERIC_ARL_DRM618):
#	nbmake-<arch> -C sys/modules/vgem618 dependall
# modload drmkms618 first.  Not part of the default module set.

.include "../Makefile.inc"

KMOD=	vgem618

WARNS=	3

.PATH:	${S}/external/bsd/drm2/vgem

.include "../drmkms618/Makefile.inc"

SRCS+=	vgem.c

CPPFLAGS+=	-DVGEM_CORE618 -DVGEM_NEWAPI

.include <bsd.kmodule.mk>
