# SPDX-License-Identifier: BSD-2-Clause
# Stateless cached ICAC diagnostic. It performs no hardware observation.
.include <bsd.init.mk>

.if !defined(EMBER_KERNEL_OBJDIR) || !exists(${EMBER_KERNEL_OBJDIR}/Makefile)
.error "Set EMBER_KERNEL_OBJDIR to the selected configured kernel object directory"
.endif

_ICAC_STATUS_CPPFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} DESTDIR=${DESTDIR:Q} -V '$${CPPFLAGS}'
_ICAC_STATUS_S!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${S}'
_ICAC_STATUS_CWARNFLAGS!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${CWARNFLAGS}'
_ICAC_STATUS_ARCH!= ${MAKE} -s -C ${EMBER_KERNEL_OBJDIR:Q} -V '$${MACHINE_ARCH}'
.if empty(_ICAC_STATUS_CPPFLAGS) || ${_ICAC_STATUS_ARCH} != "aarch64" || \
    !exists(${_ICAC_STATUS_S}/sys/systm.h)
.error "The selected kernel must provide its AArch64 preprocessing contract"
.endif
S:= ${_ICAC_STATUS_S}
# Bind to the selected kernel's generated headers and preprocessing definitions.
CPPFLAGS:= -I${EMBER_KERNEL_OBJDIR:Q} ${_ICAC_STATUS_CPPFLAGS}
KMOD= bcm2712_iommuc_status
SRCS= bcm2712_iommuc_status.c

.include <bsd.kmodule.mk>
CWARNFLAGS:= ${_ICAC_STATUS_CWARNFLAGS}
