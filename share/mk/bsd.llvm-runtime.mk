# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

.if !defined(_BSD_LLVM_RUNTIME_MK_)
_BSD_LLVM_RUNTIME_MK_=1

# The SDK validates the common Ports runtime's source, target and artifacts.
# Native generators and freestanding links must not acquire target runtimes.
.if defined(EMBER_EXTERNAL_LLVM_RUNTIME) && !defined(HOSTPROG) && !defined(HOSTLIB) && \
    empty(LDFLAGS:M-nostdlib) && empty(CPPFLAGS:M-D_STANDALONE) && \
    empty(CPPFLAGS:M-D_KERNEL)
.if ${EMBER_COMPILER_FAMILY} != "clang" || !defined(EXTERNAL_TOOLCHAIN) || \
    ${USETOOLS} != "yes" || ${ACTIVE_CC} != "clang" || ${ACTIVE_CXX} != "clang"
.error EMBER_EXTERNAL_LLVM_RUNTIME requires the explicit external Clang toolchain
.endif
.if !empty(MLIBDIR:U)
.error EMBER_EXTERNAL_LLVM_RUNTIME requires a matching non-multilib stage
.endif
.if !defined(EMBER_LLVM_RESOURCE_DIR)
.error EMBER_EXTERNAL_LLVM_RUNTIME requires the manifest-selected EMBER_LLVM_RESOURCE_DIR
.endif
.for _ember_llvm_path in EMBER_EXTERNAL_LLVM_RUNTIME EMBER_LLVM_RESOURCE_DIR
.if empty(${_ember_llvm_path}:tW:M/*) || \
    "${${_ember_llvm_path}:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${${_ember_llvm_path}})
.error ${_ember_llvm_path} must be a prepared absolute path without whitespace or shell metacharacters
.endif
.endfor
_EMBER_LLVM_RUNTIME_PHASE=${EMBER_LLVM_RUNTIME_PHASE:Ufull}
.if ${_EMBER_LLVM_RUNTIME_PHASE} == "bootstrap"
.if ${MKCXX} != "no" || ${MKLIBCXX} != "no" || \
    defined(PROG_CXX) || !empty(PROGS_CXX:U) || \
    (defined(LIBISCXX) && ${LIBISCXX} != "no")
.error The bootstrap LLVM runtime requires MKCXX=no and C-only consumers
.endif
_EMBER_LLVM_FILES= lib/libunwind.a
.elif ${_EMBER_LLVM_RUNTIME_PHASE} == "full"
.if ${MKLIBCXX} != "no" && ${MKCXX} != "yes"
.error The base LLVM C++ runtime requires MKCXX=yes
.endif
_EMBER_LLVM_FILES= include/c++/v1/vector include/c++/v1/__config_site \
    lib/libc++.a lib/libc++.so lib/libc++abi.a lib/libc++abi.so \
    lib/libunwind.a lib/libunwind.so
.else
.error EMBER_LLVM_RUNTIME_PHASE must be bootstrap or full
.endif
.for _ember_llvm_file in ${_EMBER_LLVM_FILES}
.if !exists(${EMBER_EXTERNAL_LLVM_RUNTIME}/${_ember_llvm_file})
.error EMBER_EXTERNAL_LLVM_RUNTIME is missing ${_ember_llvm_file}
.endif
.endfor

_EMBER_LLVM_RUNTIME=yes
.if ${_EMBER_LLVM_RUNTIME_PHASE} == "full"
LIBCXX?=	${EMBER_EXTERNAL_LLVM_RUNTIME}/lib/libc++.a
_EMBER_LLVM_CXX_CPPFLAGS= -nostdinc++ -isystem ${EMBER_EXTERNAL_LLVM_RUNTIME}/include/c++/v1
CXX+=		-stdlib=libc++
CXXFLAGS+=	${_EMBER_LLVM_CXX_CPPFLAGS}
MKDEPCXXFLAGS+=	${_EMBER_LLVM_CXX_CPPFLAGS}
.endif
# The common Clang NetBSD driver adds compiler-rt at the correct position
# after default libraries. A raw archive in LDADD is not equivalent for
# static C++ links. These options do not replace any OS CRT object.
LDFLAGS+=	-resource-dir=${EMBER_LLVM_RESOURCE_DIR} \
		--rtlib=compiler-rt --unwindlib=libunwind \
		-L${EMBER_EXTERNAL_LLVM_RUNTIME}/lib \
		-Wl,-rpath-link,${EMBER_EXTERNAL_LLVM_RUNTIME}/lib
.if ${_EMBER_LLVM_RUNTIME_PHASE} == "full"
LDFLAGS+=	-Wl,-rpath,/usr/pkg/llvm/lib
.endif
.endif

.endif
