# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.

.include <bsd.own.mk>

USE_FORT?= yes	# network client

PROG=		rtlbtfw
SRCS=		rtlbtfw.c rtlbt_fw.c firmload.c
MAN=		rtlbtfw.8

# Reuse firmload.c of btattach(8); keep both directories in step.
.PATH:		${.CURDIR}/../btattach
CPPFLAGS+=	-I${.CURDIR}/../btattach

DPADD+=		${LIBBLUETOOTH}
LDADD+=		-lbluetooth

.include <bsd.prog.mk>
