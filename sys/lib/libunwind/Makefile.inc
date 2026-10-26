#	$NetBSD: Makefile.inc,v 1.14 2021/11/22 16:41:32 thorpej Exp $

.include <bsd.llvm-source.mk>

.if defined(EMBER_LLVM_SOURCE)
# Ports builds the complete, position-independent archive using the common
# libunwind recipe. Keep unwind symbols in both libc archives and libc.so;
# adding a shared-library dependency would change the existing libc ABI.
.for _ember_unwind_header in unwind.h __libunwind_config.h \
    unwind_itanium.h unwind_arm_ehabi.h
.if !exists(${EMBER_LIBUNWIND_SOURCE}/include/${_ember_unwind_header})
.error EMBER_LLVM_SOURCE is missing libunwind/include/${_ember_unwind_header}
.endif
INCS+=	${EMBER_LIBUNWIND_SOURCE}/include/${_ember_unwind_header}
.endfor

# Header installation and object-directory creation precede runtime builds.
.if empty(.TARGETS) || !empty(.TARGETS:Nincludes:Nobj)
.if !defined(EMBER_LIBUNWIND_ARCHIVE)
.error EMBER_LLVM_SOURCE requires EMBER_LIBUNWIND_ARCHIVE for libc unwind support
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
llvm_unwind.o llvm_unwind.pico: ${EMBER_LIBUNWIND_ARCHIVE}
	${_MKTARGET_LINK}
	${LD} -r -o ${.TARGET}.tmp --whole-archive ${EMBER_LIBUNWIND_ARCHIVE} --no-whole-archive
	mv -f ${.TARGET}.tmp ${.TARGET}
CLEANFILES+=	llvm_unwind.o.tmp llvm_unwind.pico.tmp
.endif

.else
.if defined(EMBER_LIBUNWIND_ARCHIVE)
.error EMBER_LIBUNWIND_ARCHIVE requires EMBER_LLVM_SOURCE for matching public headers
.endif
.PATH:	${NETBSDSRCDIR}/sys/lib/libunwind

SRCS+=	libunwind.cxx \
	unwind_registers.S

INCS+=	unwind.h

COPTS.libunwind.cxx+=	${${ACTIVE_CXX} == "gcc":? -std=c++0x : -std=c++11 }
COPTS.libunwind.cxx+=	-funwind-tables -fno-rtti
COPTS.libunwind.cxx+=	-fno-exceptions -fvisibility=hidden
COPTS.libunwind.cxx+=	-Wno-old-style-cast
CPPFLAGS.libunwind.cxx+=-I${NETBSDSRCDIR}/sys/lib/libunwind

.if ${LIBC_MACHINE_CPU} == "arm"
COPTS.libunwind.cxx+=	${${ACTIVE_CC} == "gcc":? -mfpu=vfp3 :}
AFLAGS.unwind_registers.S+=	${${ACTIVE_CC} == "clang":? -mfpu=vfp3 :}
.endif
.endif
