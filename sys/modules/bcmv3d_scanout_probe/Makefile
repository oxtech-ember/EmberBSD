# SPDX-License-Identifier: BSD-2-Clause
# Explicit opt-in native HVS scanout cycle. No default attachment.
.include <bsd.init.mk>

.if !defined(EMBER_KERNEL_OBJDIR) || !exists(${EMBER_KERNEL_OBJDIR}/Makefile)
.error "Set EMBER_KERNEL_OBJDIR to the selected configured kernel object directory"
.endif

# Use the selected kernel's real preprocessing contract. Generic drmkms module
# defaults can select different architecture and legacy configuration definitions.
_SCANOUT_KERNEL_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} DESTDIR=${DESTDIR:Q} -V '$${CPPFLAGS}'
_SCANOUT_DRM_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CPPFLAGS.drmkms}'
_SCANOUT_KERNEL_S!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${S}'
_SCANOUT_KERNEL_CWARNFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CWARNFLAGS}'
_SCANOUT_KERNEL_ARCH!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${MACHINE_ARCH}'
.if empty(_SCANOUT_KERNEL_CPPFLAGS) || empty(_SCANOUT_DRM_CPPFLAGS) || \
    ${_SCANOUT_KERNEL_ARCH} != "aarch64" || !exists(${_SCANOUT_KERNEL_S}/sys/systm.h)
.error "The selected kernel must provide its AArch64 DRM preprocessing contract"
.endif
S:= ${_SCANOUT_KERNEL_S}
# Replace the generic module include defaults with the selected kernel's flags.
# The absolute object include selects its generated machine and option headers.
CPPFLAGS:= -I${EMBER_KERNEL_OBJDIR:Q} ${_SCANOUT_KERNEL_CPPFLAGS} ${_SCANOUT_DRM_CPPFLAGS}
CPPFLAGS+= -I${S}/external/bsd/drm2/dist/drm
CPPFLAGS+= -I${S}/external/bsd/drm2/bcmv3d

KMOD= bcmv3d_scanout_probe
# Use only the kernel-resident native HVS consumer.
SRCS= bcmv3d_scanout_probe.c

.include <bsd.kmodule.mk>

# Keep the selected kernel's warning policy for the same imported headers.
CWARNFLAGS:= ${_SCANOUT_KERNEL_CWARNFLAGS}
