# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

# Ports owns LLVM source acquisition, version selection, and the common patch
# set. Native OS consumers keep their ABI-specific object lists and flags.
.if !defined(_BSD_LLVM_SOURCE_MK_)
_BSD_LLVM_SOURCE_MK_=1

.if defined(EMBER_LLVM_SOURCE)
.if empty(EMBER_LLVM_SOURCE:tW:M/*) || \
    "${EMBER_LLVM_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != ""
.error EMBER_LLVM_SOURCE must be an absolute path without whitespace or shell metacharacters
.endif
.if !exists(${EMBER_LLVM_SOURCE}/runtimes/CMakeLists.txt)
.error EMBER_LLVM_SOURCE must name the prepared LLVM monorepo root
.endif
EMBER_COMPILER_RT_SOURCE=	${EMBER_LLVM_SOURCE}/compiler-rt
EMBER_LIBUNWIND_SOURCE=	${EMBER_LLVM_SOURCE}/libunwind
.endif

.endif
