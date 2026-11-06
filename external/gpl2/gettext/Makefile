# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
.include <bsd.own.mk>

.if defined(EMBER_GETTEXT_SOURCE) && defined(EMBER_LIBXML2_SOURCE) && defined(EMBER_TREE_SITTER_SOURCE)
# The private common libraries must be ready before the gettext consumer
# resolves its target pkg-config metadata.
SUBDIR= ../../bsd/libxml2/gnu ../../mit/tree-sitter/lib .WAIT gnu info
.if ${MKNLS} != "no"
SUBDIR+= locale
.endif
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_GETTEXT_SOURCE and EMBER_LIBXML2_SOURCE and EMBER_TREE_SITTER_SOURCE to the prepared common Ports sources
.endif
SUBDIR=
.endif
.include <bsd.subdir.mk>
