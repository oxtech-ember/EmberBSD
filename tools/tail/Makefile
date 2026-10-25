# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted). Native libc bootstrap entry.

HOSTPROGNAME=   ${_TOOL_PREFIX}tail
HOST_SRCDIR=    usr.bin/tail
NOCOMPATLIB=    yes
HOST_CPPFLAGS+= -include ${NETBSDSRCDIR}/tools/compat/bootstrap_cdefs.h

.include "${.CURDIR}/../Makefile.host"
