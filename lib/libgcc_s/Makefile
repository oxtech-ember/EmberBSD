# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 The EmberBSD Project contributors.
# Developed with AI assistance.

# GCC-compatible unwind library for programs that link with -lgcc_s, for
# example the Rust standard library. It contains the common Ports-prepared
# LLVM libunwind archive that libc also embeds, and exports the GCC symbol
# versions. libc keeps its own copy, so the libc ABI does not change.

USE_SHLIBDIR=	yes
NOLINT=		# defined

LIB=		gcc_s
NOSTATICLIB=	# defined
NOPROFILE=	# defined
NOPICINSTALL=	# defined

.include <bsd.own.mk>

.if ${HAVE_LLVM_LIBGCC_S} == "yes"

SHLIB_MAJOR=	1
SHLIB_MINOR=	0

.include "${NETBSDSRCDIR}/sys/lib/libunwind/archive.mk"

# The map lists only the DWARF unwinder interface of GCC libgcc_s. It does
# not supply __gcc_personality_v0, the *_frame_info functions or the libgcc
# arithmetic functions. --no-undefined-version makes a missing symbol a link
# error.
# Like GCC libgcc_s, the library has no DT_NEEDED entry for libc. bsd.lib.mk
# links gcc* libraries with -nodefaultlibs, and the run-time linker binds the
# libc symbols from the process.
# Like GCC libgcc_s, keep the library loaded: registered frames and cached
# FDEs point into it.
LDFLAGS+=	-Wl,-z,nodelete
LDFLAGS+=	-Wl,--version-script=${.CURDIR}/libgcc_s.map
LDFLAGS+=	-Wl,--no-undefined-version
DPADD+=		${.CURDIR}/libgcc_s.map

.include <bsd.lib.mk>
.else
.include <bsd.prog.mk> # do nothing
.endif
