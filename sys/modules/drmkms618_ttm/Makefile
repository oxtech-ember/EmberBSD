# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# TTM of Linux 6.18 as a loadable module.  Not
# part of the default module set; build with
#	nbmake-<arch> -C sys/modules/drmkms618_ttm dependall
# and load it after drmkms618 (the dependency says so).
#
# Not built from dist618: ttm_pool.c (the page allocator of Linux, with
# dma_map_page, set_pages_wc, shrinker: replaced by drm618/ttm_pool_bsd.c on
# uvm pages and bus_dma), ttm_backup.c (shmem writeback: drm618/ttm_backup_bsd.c),

.include "../Makefile.inc"

KMOD=	drmkms618_ttm

.PATH:	${S}/external/bsd/drm2/drm618
.PATH:	${S}/external/bsd/drm2/dist618/drm/ttm

.include "../drmkms618/Makefile.inc"

# Module glue and the BSD replacements.
SRCS+=	drm618_ttm_module.c
SRCS+=	ttm_pool_bsd.c
SRCS+=	ttm_backup_bsd.c
SRCS+=	ttm_bo_vm_bsd.c
SRCS+=	drm_gem_ttm_helper.c

# Linux 6.18 files (dist618).
SRCS+=	ttm_bo.c
SRCS+=	ttm_bo_util.c
SRCS+=	ttm_device.c
SRCS+=	ttm_execbuf_util.c
SRCS+=	ttm_module.c
SRCS+=	ttm_range_manager.c
SRCS+=	ttm_resource.c
SRCS+=	ttm_sys_manager.c
SRCS+=	ttm_tt.c

COPTS.ttm_resource.c+=	-Wno-format
COPTS.ttm_range_manager.c+=	-Wno-unused-function

WARNS=	3

.include <bsd.kmodule.mk>
