# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# The Linux 6.18 DRM core as a loadable module.
# Not part of the default module set (not listed in ../Makefile), not
# connected to any default kernel: build with
#	nbmake-<arch> -C sys/modules/drmkms618 dependall
# and load it into a kernel without the 5.6 core (configuration
# GENERIC_ARL_DRM618): both cores export the same names.
#
# The file list is dist618/MANIFEST.tsv minus drm618/tools/not-built.txt;
# the warning waivers of the Linux files follow the 5.6 module (drmkms): the
# Linux sources are not edited to please -Wshadow and friends, and -Wformat
# does not know that a Linux u64 is unsigned long long.

.include "../Makefile.inc"

KMOD=	drmkms618

.PATH:	${S}/external/bsd/drm2/drm618
.PATH:	${S}/external/bsd/drm2/dist618/drm
.PATH:	${S}/external/bsd/drm2/dist618/drm/display
.PATH:	${S}/external/bsd/drm2/dist618/drm/clients
.PATH:	${S}/external/bsd/drm2/dist618/gpu

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

# The one source that sees both NetBSD's struct file and the Linux one.
CPPFLAGS.drm618_file.c+=	-DDRM618_NBSD_FILE

# Own code of the 6.18 core, and the module glue.
SRCS+=	drm618_cache.c
SRCS+=	drm618_module.c
SRCS+=	drm618_file.c
SRCS+=	drm618_kthread_worker.c
SRCS+=	drm618_managed.c
SRCS+=	drm618_module.c
SRCS+=	drm618_prime_stub.c
SRCS+=	drm618_rbtree.c
SRCS+=	drm618_shmem.c
SRCS+=	drm618_sleep.c
SRCS+=	drm618_statics.c
SRCS+=	drm618_sync_file.c
SRCS+=	drm618_sysfs.c

# Linux 6.18 files (dist618).
SRCS+=	drm_client_setup.c
SRCS+=	drm_display_helper_mod.c
SRCS+=	drm_dp_dual_mode_helper.c
SRCS+=	drm_dp_helper.c
SRCS+=	drm_dp_mst_topology.c
SRCS+=	drm_dsc_helper.c
SRCS+=	drm_hdmi_helper.c
SRCS+=	drm_hdmi_state_helper.c
SRCS+=	drm_scdc_helper.c
SRCS+=	drm_atomic.c
SRCS+=	drm_atomic_helper.c
SRCS+=	drm_atomic_state_helper.c
SRCS+=	drm_atomic_uapi.c
SRCS+=	drm_auth.c
SRCS+=	drm_blend.c
SRCS+=	drm_bridge.c
SRCS+=	drm_buddy.c
SRCS+=	drm_client.c
SRCS+=	drm_client_event.c
SRCS+=	drm_client_modeset.c
SRCS+=	drm_color_mgmt.c
SRCS+=	drm_connector.c
SRCS+=	drm_crtc.c
SRCS+=	drm_crtc_helper.c
SRCS+=	drm_damage_helper.c
SRCS+=	drm_displayid.c
SRCS+=	drm_drv.c
SRCS+=	drm_dumb_buffers.c
SRCS+=	drm_edid.c
SRCS+=	drm_eld.c
SRCS+=	drm_encoder.c
SRCS+=	drm_exec.c
SRCS+=	drm_file.c
SRCS+=	drm_flip_work.c
SRCS+=	drm_format_helper.c
SRCS+=	drm_fourcc.c
SRCS+=	drm_framebuffer.c
SRCS+=	drm_gem.c
SRCS+=	drm_gpuvm.c
SRCS+=	drm_ioctl.c
SRCS+=	drm_kms_helper_common.c
SRCS+=	drm_mm.c
SRCS+=	drm_mode_config.c
SRCS+=	drm_mode_object.c
SRCS+=	drm_modes.c
SRCS+=	drm_modeset_helper.c
SRCS+=	drm_modeset_lock.c
SRCS+=	drm_panel.c
SRCS+=	drm_panel_orientation_quirks.c
SRCS+=	drm_pci.c
SRCS+=	drm_plane.c
SRCS+=	drm_plane_helper.c
SRCS+=	drm_print.c
SRCS+=	drm_probe_helper.c
SRCS+=	drm_property.c
SRCS+=	drm_rect.c
SRCS+=	drm_self_refresh_helper.c
SRCS+=	drm_suballoc.c
SRCS+=	drm_syncobj.c
SRCS+=	drm_vblank.c
SRCS+=	drm_vblank_helper.c
SRCS+=	drm_vblank_work.c
SRCS+=	drm_vma_manager.c
SRCS+=	buddy.c

# Warning waivers of Linux files (generated from the warnings of the compile map).
COPTS.buddy.c+=	-Wno-cast-qual
COPTS.buddy.c+=	-Wno-format
COPTS.drm_atomic.c+=	-Wno-format
COPTS.drm_atomic_uapi.c+=	-Wno-format
COPTS.drm_atomic_uapi.c+=	-Wno-shadow
COPTS.drm_blend.c+=	-Wno-cast-qual
COPTS.drm_bridge.c+=	-Wno-pointer-arith
COPTS.drm_bridge.c+=	-Wno-shadow
COPTS.drm_buddy.c+=	-Wno-format
COPTS.drm_client_modeset.c+=	-Wno-cast-qual
COPTS.drm_client_modeset.c+=	-Wno-shadow
COPTS.drm_color_mgmt.c+=	-Wno-pointer-arith
COPTS.drm_connector.c+=	-Wno-incompatible-pointer-types-discards-qualifiers
COPTS.drm_crtc.c+=	-Wno-format
COPTS.drm_crtc.c+=	-Wno-pointer-arith
COPTS.drm_dp_dual_mode_helper.c+=	-Wno-pointer-arith
COPTS.drm_dp_helper.c+=	-Wno-pointer-arith
COPTS.drm_dp_mst_topology.c+=	-Wno-shadow
COPTS.drm_drv.c+=	-Wno-pointer-arith
COPTS.drm_edid.c+=	-Wno-cast-qual
COPTS.drm_edid.c+=	-Wno-format
COPTS.drm_edid.c+=	-Wno-incompatible-pointer-types-discards-qualifiers
COPTS.drm_edid.c+=	-Wno-pointer-arith
COPTS.drm_edid.c+=	-Wno-shadow
COPTS.drm_encoder.c+=	-Wno-pointer-arith
COPTS.drm_file.c+=	-Wno-format
COPTS.drm_file.c+=	-Wno-incompatible-pointer-types-discards-qualifiers
COPTS.drm_format_helper.c+=	-Wno-pointer-arith
COPTS.drm_framebuffer.c+=	-Wno-format
COPTS.drm_gpuvm.c+=	-Wno-shadow
COPTS.drm_ioctl.c+=	-Wno-incompatible-pointer-types-discards-qualifiers
COPTS.drm_ioctl.c+=	-Wno-shadow
COPTS.drm_mm.c+=	-Wno-cast-qual
COPTS.drm_mm.c+=	-Wno-format
COPTS.drm_mode_config.c+=	-Wno-shadow
COPTS.drm_modes.c+=	-Wno-cast-qual
COPTS.drm_modes.c+=	-Wno-shadow
COPTS.drm_panel.c+=	-Wno-pointer-arith
COPTS.drm_panel.c+=	-Wno-unused-value
COPTS.drm_plane.c+=	-Wno-format
COPTS.drm_plane.c+=	-Wno-pointer-arith
COPTS.drm_print.c+=	-Wno-incompatible-pointer-types-discards-qualifiers
COPTS.drm_print.c+=	-Wno-pointer-arith
COPTS.drm_probe_helper.c+=	-Wno-format
COPTS.drm_property.c+=	-Wno-format
COPTS.drm_property.c+=	-Wno-pointer-arith
COPTS.drm_scdc_helper.c+=	-Wno-pointer-arith
COPTS.drm_syncobj.c+=	-Wno-shadow
COPTS.drm_vblank.c+=	-Wno-format
COPTS.drm_vblank_work.c+=	-Wno-format

WARNS=	3

.include <bsd.kmodule.mk>
