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
SRCS+=	drm2-linux-618-layer.c
SRCS+=	drm2-drm618-managed.c
SRCS+=	drm2-drm618-statics.c
SRCS+=	drm2-drm618-file.c
SRCS+=	drm2-drm618-kdev.c
SRCS+=	drm2-drm618-task.c
SRCS+=	drm2-drm618-gem.c
SRCS+=	drm2-drm618-kms.c
SRCS+=	drm2-drm618-hdr.c
SRCS+=	drm2-p4-layer.c

# The 6.18-only code under test is compiled into the module (the kernel has no
# drmkms618).
.PATH: ${S}/external/bsd/drm2/drm618
SRCS+=	drm618_kthread_worker.c
SRCS+=	drm618_managed.c
SRCS+=	drm618_file.c
SRCS+=	drm618_sysfs.c
SRCS+=	drm618_sleep.c
SRCS+=	drm618_shmem.c
SRCS+=	drm618_lease.c
SRCS+=	drm618_gem_atomic_helper.c
SRCS+=	drm618_unsupported.c
SRCS+=	drm618_sync_file.c
SRCS+=	drm618_vma.c

# These two read struct drm_device, so they get the includes the 6.18 core
# expects (files.drmkms618).
CPPFLAGS.drm618_managed.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-statics.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-managed.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h

# The kernel of the test VM has the 5.6 core built in: the three names of the
# 6.18 drm_sysfs functions that it defines too are renamed in the two sources
# that have them (the 6.18 core itself is not loaded here).
DRM618_T_RENAME=	-Ddrm_sysfs_connector_add=t618_drm_sysfs_connector_add \
		-Ddrm_sysfs_connector_remove=t618_drm_sysfs_connector_remove \
		-Ddrm_sysfs_hotplug_event=t618_drm_sysfs_hotplug_event

# drm618_file.c sees both kinds of file (see drm618_file.h).
CPPFLAGS.drm618_file.c+=	-D__KERNEL__ -DDRM618_NBSD_FILE -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_sysfs.c+=	${DRM618_T_RENAME} -D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_sleep.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_sync_file.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_vma.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_shmem.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-kms.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-p4-layer.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-hdr.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_lease.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_gem_atomic_helper.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm618_unsupported.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-gem.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-task.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-kdev.c+=	${DRM618_T_RENAME} -D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h
CPPFLAGS.drm2-drm618-file.c+=	-D__KERNEL__ -include ${S}/external/bsd/drm2/drm618/include/drm618_prefix.h

WARNS=	3

.include <bsd.kmodule.mk>
