# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

# Link the common Ports-prepared libunwind PIC archive into one relocatable
# object. libc and libgcc_s use this rule with the same archive.
# Header installation and object-directory creation precede runtime builds.
.if empty(.TARGETS) || !empty(.TARGETS:Nincludes:Nobj)
.if !defined(EMBER_LIBUNWIND_ARCHIVE)
.error EMBER_LLVM_SOURCE requires EMBER_LIBUNWIND_ARCHIVE for unwind support
.endif
.if empty(EMBER_LIBUNWIND_ARCHIVE:tW:M/*) || \
    "${EMBER_LIBUNWIND_ARCHIVE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_LIBUNWIND_ARCHIVE})
.error EMBER_LIBUNWIND_ARCHIVE must name the prepared absolute PIC archive
.endif

# A regular PIC archive cannot provide instrumented profiling/debug variants.
.if ${MKPROFILE} != "no" || ${MKDEBUGLIB} != "no"
.error EMBER_LIBUNWIND_ARCHIVE requires MKPROFILE=no and MKDEBUGLIB=no
.endif
OBJS+=	llvm_unwind.o
# libc owns thread stubs; unwind autolink hints must not add libpthread.
llvm_unwind.o llvm_unwind.pico: ${EMBER_LIBUNWIND_ARCHIVE}
	${_MKTARGET_LINK}
	${LD} -r -o ${.TARGET}.tmp --whole-archive ${EMBER_LIBUNWIND_ARCHIVE} --no-whole-archive
	${OBJCOPY} --remove-section=.deplibs ${.TARGET}.tmp
	mv -f ${.TARGET}.tmp ${.TARGET}
CLEANFILES+=	llvm_unwind.o.tmp llvm_unwind.pico.tmp
.endif
