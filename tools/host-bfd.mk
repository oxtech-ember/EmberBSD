# SPDX-License-Identifier: BSD-2-Clause
# Origin: EmberBSD (AI-assisted).
# Ordinary make inputs from the common Ports Binutils producer.
.if defined(EXTERNAL_TOOLCHAIN) && defined(EMBER_NATIVE_TOOLS) && \
    !defined(EMBER_HOST_BFD_CONFIG)
.error Explicit native tools require EMBER_HOST_BFD_CONFIG for external-toolchain helpers
.endif
.if defined(EMBER_HOST_BFD_CONFIG) && !defined(_EMBER_HOST_BFD_INCLUDED)
_EMBER_HOST_BFD_INCLUDED= yes
.if !defined(EXTERNAL_TOOLCHAIN)
.error Common host BFD requires an external toolchain
.endif
.if empty(EMBER_HOST_BFD_CONFIG:M/*) || !exists(${EMBER_HOST_BFD_CONFIG})
.error Missing or non-absolute common host BFD configuration
.endif
.include "${EMBER_HOST_BFD_CONFIG}"
.for field in TRIPLET SOURCE BUILD CPPFLAGS LDADD DPADD
.if empty(EMBER_HOST_BFD_${field})
.error Missing common host BFD field ${field}
.endif
.endfor
.if "${EMBER_HOST_BFD_TRIPLET}" != "${MACHINE_GNU_PLATFORM}"
.error Common host BFD target differs from MACHINE_GNU_PLATFORM
.endif
.for field in SOURCE BUILD
.if empty(EMBER_HOST_BFD_${field}:M/*)
.error Common host BFD ${field} must be absolute
.endif
.endfor
.for input in ${EMBER_HOST_BFD_SOURCE}/include/elf/common.h \
    ${EMBER_HOST_BFD_SOURCE}/include/elf/external.h \
    ${EMBER_HOST_BFD_SOURCE}/include/ansidecl.h \
    ${EMBER_HOST_BFD_SOURCE}/include/symcat.h \
    ${EMBER_HOST_BFD_BUILD}/bfd/bfd.h ${EMBER_HOST_BFD_DPADD}
.if "${input:M/*}" == "" || !exists(${input})
.error Missing or non-absolute common host BFD input ${input}
.endif
.endfor
.endif
