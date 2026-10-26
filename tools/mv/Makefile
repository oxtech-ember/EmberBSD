# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).

HOSTPROGNAME=	${_TOOL_PREFIX}mv
HOST_SRCDIR=	bin/mv

# These fixed executable paths are compiled into the host program.
# Restrict selector characters instead of interpreting shell expressions.
.for _mv_child in CP RM
.if !defined(HOST_MV_${_mv_child}) || empty(HOST_MV_${_mv_child}:M/*) || \
    !empty(HOST_MV_${_mv_child}:C,[-a-zA-Z0-9_./+],,g)
.error "Select an absolute HOST_MV_${_mv_child} path using plain path characters"
.endif
.if !exists(${HOST_MV_${_mv_child}})
.error "HOST_MV_${_mv_child} does not exist"
.endif
CPPFLAGS+=	-DEMBERBSD_MV_${_mv_child}='"${HOST_MV_${_mv_child}}"'
.endfor

.include "${.CURDIR}/../Makefile.host"
