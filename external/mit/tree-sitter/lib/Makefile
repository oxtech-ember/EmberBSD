# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
LIBISPRIVATE=pic
.include <bsd.own.mk>

.if defined(EMBER_TREE_SITTER_SOURCE)
LIB= tree-sitter
MAKESTATICLIB=yes
MKLINT=no
SRCS= lib.c
.PATH: ${EMBER_TREE_SITTER_SOURCE}/lib/src
CPPFLAGS+= -I${EMBER_TREE_SITTER_SOURCE}/lib/include \
    -I${EMBER_TREE_SITTER_SOURCE}/lib/src -I${EMBER_TREE_SITTER_SOURCE}/lib/src/wasm \
    -D_POSIX_C_SOURCE=200112L -D_DEFAULT_SOURCE -D_BSD_SOURCE
CFLAGS+= -std=c11 -fvisibility=hidden

# Uninstalled metadata keeps target object paths outside the sysroot prefix.
realall: tree-sitter-uninstalled.pc
CLEANFILES+= tree-sitter-uninstalled.pc
.PHONY: tree-sitter-pc-inputs
tree-sitter-pc-inputs:
tree-sitter-uninstalled.pc: tree-sitter-pc-inputs ${EMBER_TREE_SITTER_SOURCE}/Makefile ${.CURDIR}/tree-sitter-uninstalled.pc.in
	@version=`${TOOL_SED} -n 's/^VERSION := //p' ${EMBER_TREE_SITTER_SOURCE:Q}/Makefile`; \
	    test -n "$$version" && \
	    ${TOOL_SED} -e "s|@VERSION@|$$version|" \
	    -e 's|@LIBDIR@|${.OBJDIR}|g' \
	    -e 's|@INCLUDEDIR@|${EMBER_TREE_SITTER_SOURCE}/lib/include|g' \
	    ${.CURDIR:Q}/tree-sitter-uninstalled.pc.in > ${.TARGET:Q}.tmp && \
	    { cmp -s ${.TARGET:Q}.tmp ${.TARGET:Q} && rm ${.TARGET:Q}.tmp || \
	      mv ${.TARGET:Q}.tmp ${.TARGET:Q}; }
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall))
.error Set EMBER_TREE_SITTER_SOURCE to the prepared common Ports tree-sitter source
.endif
.endif
.include <bsd.lib.mk>
