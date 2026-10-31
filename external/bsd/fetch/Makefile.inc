# $NetBSD: Makefile.inc,v 1.1 2008/09/30 19:09:00 joerg Exp $

.include <bsd.own.mk>

.if !defined(EMBER_LIBFETCH_SOURCE) && !(make(obj) && empty(.TARGETS:Nobj))
.error Set EMBER_LIBFETCH_SOURCE to the prepared common Ports source
.endif
.if defined(EMBER_LIBFETCH_SOURCE)
LIBFETCHDIR=	${EMBER_LIBFETCH_SOURCE}
.else
LIBFETCHDIR=	${.CURDIR}/../dist/libfetch
.endif

USE_FORT?=	yes # complex string handling

WARNS?=	4
