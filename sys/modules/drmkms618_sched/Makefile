# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# The GPU scheduler of Linux 6.18 (drm_sched) as a loadable module.  Not part of the default module set; build with
#	nbmake-<arch> -C sys/modules/drmkms618_sched dependall
# and load it after drmkms618 (the dependency says so).

.include "../Makefile.inc"

KMOD=	drmkms618_sched

.PATH:	${S}/external/bsd/drm2/drm618
.PATH:	${S}/external/bsd/drm2/dist618/drm/scheduler

.include "../drmkms618/Makefile.inc"

# Module glue.
SRCS+=	drm618_sched_module.c

# Linux 6.18 files (dist618).
SRCS+=	sched_entity.c
SRCS+=	sched_fence.c
SRCS+=	sched_main.c

COPTS.sched_main.c+=	-Wno-cast-qual

WARNS=	3

.include <bsd.kmodule.mk>
