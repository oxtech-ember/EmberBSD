# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# Test module for the drm2 Linux compatibility layer: modload runs the checks
# of ember/tests/drm2-linux-compat.c against the real layer headers and fails
# with EIO if any check fails. Not part of the default module set; build with
#	nbmake-<arch> -C sys/modules/drmkms_linux_test dependall

.include "../Makefile.inc"

.PATH: ${S}/../ember/tests

CPPFLAGS+=	-I${S}/external/bsd/common/include
CPPFLAGS+=	-I${S}/external/bsd/drm2/include
CPPFLAGS+=	-DDIAGNOSTIC

# Negative control: nbmake ... SELFTEST_FAIL=1 builds a module that must not load.
.if defined(SELFTEST_FAIL)
CPPFLAGS+=	-DDRM2_COMPAT_SELFTEST_FAIL
.endif

KMOD=	drmkms_linux_test

SRCS+=	drmkms_linux_test.c
SRCS+=	drm2-linux-compat.c
SRCS+=	drm2-dma-resv.c
SRCS+=	drm2-linux-idr.c
SRCS+=	drm2-linux-sync.c

# The wait queue API under test lives in the 6.18-only header directory; the
# shared layer has an empty <linux/wait.h> that comes first in the path.
CPPFLAGS.drm2-linux-sync.c+=	-include ${S}/external/bsd/drm2/drm618/include/linux/wait.h

WARNS=	3

.include <bsd.kmodule.mk>
