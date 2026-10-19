# SPDX-License-Identifier: BSD-2-Clause
# Explicit native-provider acceptance module. No default attachment.
.include <bsd.init.mk>

.if !defined(EMBER_KERNEL_OBJDIR) || !exists(${EMBER_KERNEL_OBJDIR}/Makefile)
.error "Set EMBER_KERNEL_OBJDIR to the selected configured kernel object directory"
.endif

# Use the selected kernel's real preprocessing contract. Generic drmkms module
# defaults can select different architecture and legacy configuration definitions.
_HVS_KERNEL_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} DESTDIR=${DESTDIR:Q} -V '$${CPPFLAGS}'
_HVS_DRM_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CPPFLAGS.drmkms}'
_HVS_KERNEL_S!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${S}'
_HVS_KERNEL_CWARNFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CWARNFLAGS}'
_HVS_KERNEL_ARCH!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${MACHINE_ARCH}'
.if empty(_HVS_KERNEL_CPPFLAGS) || empty(_HVS_DRM_CPPFLAGS) || \
    ${_HVS_KERNEL_ARCH} != "aarch64" || !exists(${_HVS_KERNEL_S}/sys/systm.h)
.error "The selected kernel must provide its AArch64 DRM preprocessing contract"
.endif
S:= ${_HVS_KERNEL_S}
# Replace the generic module include defaults with the selected kernel's flags.
# The absolute object include selects its generated machine and option headers.
CPPFLAGS:= -I${EMBER_KERNEL_OBJDIR:Q} ${_HVS_KERNEL_CPPFLAGS} ${_HVS_DRM_CPPFLAGS}
CPPFLAGS+= -I${S}/external/bsd/drm2/dist/drm

KMOD= bcmv3d_hvs_prepare
# Compile only this private preparation helper from the module source revision.
# Keep S and all existing native interfaces at the selected kernel revision.
_HVS_PRIVATE_DIR:= ${.PARSEDIR}/../../external/bsd/drm2/bcmv3d
.PATH: ${_HVS_PRIVATE_DIR}
CPPFLAGS+= -I${_HVS_PRIVATE_DIR:Q}
# The encoder and backing contain no shared-cache owner or MMIO backend.
_MMU4_PRIVATE_DIR:= ${.PARSEDIR}/../../arch/arm/broadcom
.PATH: ${_MMU4_PRIVATE_DIR}
SRCS= bcmv3d_hvs_prepare_module.c bcmv3d_hvs_prepare.c \
    bcmv3d_mmu4_backing.c bcm2712_mmu4_table.c

.include <bsd.kmodule.mk>

# Keep the selected kernel's warning policy for the same imported headers.
CWARNFLAGS:= ${_HVS_KERNEL_CWARNFLAGS}
