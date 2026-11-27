#	$NetBSD: Makefile.inc,v 1.14 2021/11/22 16:41:32 thorpej Exp $

.include <bsd.llvm-source.mk>
.if !defined(EMBER_LLVM_SOURCE)
.error Set EMBER_LLVM_SOURCE to the common Ports-prepared LLVM source root
.endif

# Ports builds the complete, position-independent archive using the common
# libunwind recipe. Keep unwind symbols in both libc archives and libc.so;
# adding a shared-library dependency would change the existing libc ABI.
.for _ember_unwind_header in unwind.h __libunwind_config.h \
    unwind_itanium.h unwind_arm_ehabi.h
.if !exists(${EMBER_LIBUNWIND_SOURCE}/include/${_ember_unwind_header})
.error EMBER_LLVM_SOURCE is missing libunwind/include/${_ember_unwind_header}
.endif
INCS+=	${EMBER_LIBUNWIND_SOURCE}/include/${_ember_unwind_header}
INCSNAME_${EMBER_LIBUNWIND_SOURCE:C,/,_,g}_include_${_ember_unwind_header}= ${_ember_unwind_header}
.endfor

.include "${NETBSDSRCDIR}/sys/lib/libunwind/archive.mk"
