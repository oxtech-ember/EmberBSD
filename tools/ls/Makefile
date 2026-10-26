# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
.include <bsd.hostinit.mk>

# glibc exposes wcwidth through the XSI namespace. Keep BSD declarations too.
.if !empty(HOST_OSTYPE:MLinux-*)
HOST_CPPFLAGS+= -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE=1
.endif

HOSTPROGNAME=	${_TOOL_PREFIX}ls
HOST_SRCDIR=	bin/ls

.include "${.CURDIR}/../Makefile.host"
