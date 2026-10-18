# SPDX-License-Identifier: BSD-2-Clause
# Explicit finite read-only IOMMU observer. No default attachment.
.include <bsd.init.mk>

.if !defined(EMBER_KERNEL_OBJDIR) || !exists(${EMBER_KERNEL_OBJDIR}/Makefile)
.error "Set EMBER_KERNEL_OBJDIR to the selected configured kernel object directory"
.endif

# Use the selected kernel's real preprocessing contract. Generic drmkms module
# defaults can select different architecture and legacy configuration definitions.
_IOMMU_KERNEL_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} DESTDIR=${DESTDIR:Q} -V '$${CPPFLAGS}'
_IOMMU_DRM_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CPPFLAGS.drmkms}'
_IOMMU_KERNEL_S!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${S}'
_IOMMU_KERNEL_CWARNFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CWARNFLAGS}'
_IOMMU_KERNEL_ARCH!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${MACHINE_ARCH}'
.if empty(_IOMMU_KERNEL_CPPFLAGS) || empty(_IOMMU_DRM_CPPFLAGS) || \
    ${_IOMMU_KERNEL_ARCH} != "aarch64" || !exists(${_IOMMU_KERNEL_S}/sys/systm.h)
.error "The selected kernel must provide its AArch64 DRM preprocessing contract"
.endif
S:= ${_IOMMU_KERNEL_S}
# Replace the generic module include defaults with the selected kernel's flags.
# The absolute object include selects its generated machine and option headers.
CPPFLAGS:= -I${EMBER_KERNEL_OBJDIR:Q} ${_IOMMU_KERNEL_CPPFLAGS} ${_IOMMU_DRM_CPPFLAGS}
CPPFLAGS+= -I${S}/external/bsd/drm2/dist/drm

KMOD= bcm2712_iommu_observe
SRCS= bcm2712_iommu_observe.c

.include <bsd.kmodule.mk>

# Keep the selected kernel's warning policy for the same imported headers.
CWARNFLAGS:= ${_IOMMU_KERNEL_CWARNFLAGS}
