# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
.include <bsd.own.mk>

.if defined(EMBER_GETTEXT_SOURCE) && defined(EMBER_LIBXML2_SOURCE)
# The common static XML library is private to this build, and must be ready
# before the gettext consumer resolves its target pkg-config metadata.
SUBDIR= ../../bsd/libxml2/gnu .WAIT gnu info
.if ${MKNLS} != "no"
SUBDIR+= locale
.endif
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_GETTEXT_SOURCE and EMBER_LIBXML2_SOURCE to the prepared common Ports sources
.endif
SUBDIR=
.endif
.include <bsd.subdir.mk>
