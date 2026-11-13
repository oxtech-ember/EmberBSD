# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# Test module for the 6.18 DRM core and its helpers loaded as modules
# (drmkms618, drmkms618_sched, drmkms618_ttm): modload runs the checks of
# ember/tests/drm2-sched.c and drm2-ttm-*.c against the real modules and
# fails with EIO if any check fails.  Not part of the default module set;
# build with
#	nbmake-<arch> -C sys/modules/drmkms618_test dependall

.include "../Makefile.inc"

.PATH: ${S}/../ember/tests

KMOD=	drmkms618_test

.include "../drmkms618/Makefile.inc"

SRCS+=	drmkms618_test.c
SRCS+=	drm2-sched.c
SRCS+=	drm2-ttm.c

WARNS=	3

.include <bsd.kmodule.mk>
