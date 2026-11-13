# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# Test module for the drm2 Linux compatibility layer: modload runs the checks
# of ember/tests/drm2-linux-compat.c against the real layer headers and fails
# with EIO if any check fails. Not part of the default module set; build with
#	nbmake-<arch> -C sys/modules/drmkms_linux_test dependall

.include "../Makefile.inc"

.PATH: ${S}/../ember/tests

# The 6.18-only headers first: drm618/include/linux/wait.h shadows the empty
# <linux/wait.h> of the shared layer (see there), drm618_prefix.h is under test.
CPPFLAGS+=	-I${S}/external/bsd/drm2/drm618/include
CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/include
CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/include/uapi
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
SRCS+=	drm2-linux-618.c
SRCS+=	drm2-drm618-managed.c

# The 6.18-only code under test is compiled into the module (the kernel has no
# drmkms618).
.PATH: ${S}/external/bsd/drm2/drm618
SRCS+=	drm618_kthread_worker.c
SRCS+=	drm618_managed.c

# These two read struct drm_device, so they get the includes the 6.18 core
# expects (files.drmkms618).
CPPFLAGS.drm618_managed.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-managed.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h

WARNS=	3

.include <bsd.kmodule.mk>
