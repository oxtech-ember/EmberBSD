# SPDX-License-Identifier: BSD-2-Clause
# Explicit opt-in DISP16 lease hold. No default attachment.
.include <bsd.init.mk>

.if !defined(EMBER_KERNEL_OBJDIR) || !exists(${EMBER_KERNEL_OBJDIR}/Makefile)
.error "Set EMBER_KERNEL_OBJDIR to the selected configured kernel object directory"
.endif

# Use the selected kernel's real preprocessing contract. Generic drmkms module
# defaults can select different architecture and legacy configuration definitions.
_DISP_KERNEL_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} DESTDIR=${DESTDIR:Q} -V '$${CPPFLAGS}'
_DISP_DRM_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CPPFLAGS.drmkms}'
_DISP_KERNEL_S!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${S}'
_DISP_KERNEL_CWARNFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CWARNFLAGS}'
_DISP_KERNEL_ARCH!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${MACHINE_ARCH}'
.if empty(_DISP_KERNEL_CPPFLAGS) || empty(_DISP_DRM_CPPFLAGS) || \
    ${_DISP_KERNEL_ARCH} != "aarch64" || !exists(${_DISP_KERNEL_S}/sys/systm.h)
.error "The selected kernel must provide its AArch64 DRM preprocessing contract"
.endif
S:= ${_DISP_KERNEL_S}
# Replace the generic module include defaults with the selected kernel's flags.
# The absolute object include selects its generated machine and option headers.
CPPFLAGS:= -I${EMBER_KERNEL_OBJDIR:Q} ${_DISP_KERNEL_CPPFLAGS} ${_DISP_DRM_CPPFLAGS}
CPPFLAGS+= -I${S}/external/bsd/drm2/dist/drm

KMOD= bcm2712_disp_probe
# Compile the SAME shared engine with private function names. Audit the exact
# new clock C/header and DISP files as exceptions to the selected kernel source.
.PATH: ${.PARSEDIR}/../../arch/arm/broadcom
CPPFLAGS+= -include ${.PARSEDIR}/../../arch/arm/broadcom/bcm2712_display_clockvar.h
CPPFLAGS+= -Dbcm2712_clock_init=bcm2712_disp_private_init
CPPFLAGS+= -Dbcm2712_clock_acquire=bcm2712_disp_private_acquire
CPPFLAGS+= -Dbcm2712_clock_update=bcm2712_disp_private_update
CPPFLAGS+= -Dbcm2712_clock_release=bcm2712_disp_private_release
CPPFLAGS+= -Dbcm2712_clock_status=bcm2712_disp_private_status
CPPFLAGS+= -Dbcm2712_clock_fini=bcm2712_disp_private_fini
SRCS= bcm2712_disp_probe.c bcm2712_display_clock.c bcm2712_display_disp_acpi.c

.include <bsd.kmodule.mk>

# Keep the selected kernel's warning policy for the same imported headers.
CWARNFLAGS:= ${_DISP_KERNEL_CWARNFLAGS}
