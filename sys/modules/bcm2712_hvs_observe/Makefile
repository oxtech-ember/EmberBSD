# SPDX-License-Identifier: BSD-2-Clause
# Explicit finite read-only HVS observer. No default attachment.
.include <bsd.init.mk>

.if !defined(EMBER_KERNEL_OBJDIR) || !exists(${EMBER_KERNEL_OBJDIR}/Makefile)
.error "Set EMBER_KERNEL_OBJDIR to the selected configured kernel object directory"
.endif

# Use the selected kernel's real preprocessing contract. Generic drmkms module
# defaults can select different architecture and legacy configuration definitions.
_HVS_OBSERVE_KERNEL_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} DESTDIR=${DESTDIR:Q} -V '$${CPPFLAGS}'
_HVS_OBSERVE_DRM_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CPPFLAGS.drmkms}'
_HVS_OBSERVE_KERNEL_S!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${S}'
_HVS_OBSERVE_KERNEL_CWARNFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CWARNFLAGS}'
_HVS_OBSERVE_KERNEL_ARCH!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${MACHINE_ARCH}'
.if empty(_HVS_OBSERVE_KERNEL_CPPFLAGS) || empty(_HVS_OBSERVE_DRM_CPPFLAGS) || \
    ${_HVS_OBSERVE_KERNEL_ARCH} != "aarch64" || !exists(${_HVS_OBSERVE_KERNEL_S}/sys/systm.h)
.error "The selected kernel must provide its AArch64 DRM preprocessing contract"
.endif
S:= ${_HVS_OBSERVE_KERNEL_S}
# Replace the generic module include defaults with the selected kernel's flags.
# The absolute object include selects its generated machine and option headers.
CPPFLAGS:= -I${EMBER_KERNEL_OBJDIR:Q} ${_HVS_OBSERVE_KERNEL_CPPFLAGS} ${_HVS_OBSERVE_DRM_CPPFLAGS}
CPPFLAGS+= -I${S}/external/bsd/drm2/dist/drm

KMOD= bcm2712_hvs_observe
SRCS= bcm2712_hvs_observe.c

.include <bsd.kmodule.mk>

# Keep the selected kernel's warning policy for the same imported headers.
CWARNFLAGS:= ${_HVS_OBSERVE_KERNEL_CWARNFLAGS}
