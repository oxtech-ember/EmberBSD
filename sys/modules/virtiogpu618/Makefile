# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# The virtio-gpu driver of Linux 6.18 (dist618/drm/virtio) on the Linux 6.18
# DRM core: attaches to a virtio(4) device of type 16.  Needs the modules of
# the 6.18 core (configuration GENERIC_ARL_DRM618, which has no viogpu):
#	nbmake-<arch> -C sys/modules/virtiogpu618 dependall
# modload drmkms618 (and its helpers) first.  Not part of the default set.
#
# 2D only works: the 3D, blob and fence paths compile as in Linux, but the
# driver is given no VIRGL, blob or context-init feature by the device the
# tested way, and a host visible region is not supported (drm618_virtio.c).

.include "../Makefile.inc"

KMOD=	virtiogpu618
IOCONF=	virtiogpu618.ioconf

.PATH:	${S}/external/bsd/drm2/drm618
.PATH:	${S}/external/bsd/drm2/dist618/drm/virtio

.include "../drmkms618/Makefile.inc"

CPPFLAGS+=	-I${S}/external/bsd/drm2/dist618/drm/virtio

# Kconfig of the driver: the display part is on, debugfs is not.
CPPFLAGS+=	-DCONFIG_DRM_VIRTIO_GPU_KMS=1

WARNS=	3

# Own code.
SRCS+=	drm618_virtio.c
SRCS+=	drm618_virtio_dma_buf.c
SRCS+=	drm618_virtio_vram.c
SRCS+=	drm618_virtiogpu_module.c

# Linux 6.18 files (dist618/drm/virtio).
SRCS+=	virtgpu_debugfs.c
SRCS+=	virtgpu_display.c
SRCS+=	virtgpu_drv.c
SRCS+=	virtgpu_fence.c
SRCS+=	virtgpu_gem.c
SRCS+=	virtgpu_ioctl.c
SRCS+=	virtgpu_kms.c
SRCS+=	virtgpu_object.c
SRCS+=	virtgpu_plane.c
SRCS+=	virtgpu_prime.c
SRCS+=	virtgpu_submit.c
SRCS+=	virtgpu_vq.c

# Warning waivers of the Linux files, as for the 5.6 module: the sources are
# not edited to please these (void pointer arithmetic, shadowing of a global,
# %lld for a u64, string literals for char *).
.for f in drm618_virtio_vram.c virtgpu_debugfs.c virtgpu_display.c virtgpu_drv.c virtgpu_fence.c virtgpu_gem.c virtgpu_ioctl.c virtgpu_kms.c virtgpu_object.c virtgpu_plane.c virtgpu_prime.c virtgpu_submit.c virtgpu_vq.c
COPTS.${f}+=	-Wno-pointer-arith -Wno-shadow -Wno-format -Wno-write-strings
.endfor

.include <bsd.kmodule.mk>
