# $NetBSD: Makefile.inc,v 1.5 2023/09/04 18:12:47 riastradh Exp $

.include <bsd.own.mk>

LIBEVENT_DIR=	${NETBSDSRCDIR}/external/bsd/libevent
# The common Ports source producer applies the pinned recipe and patches.
.if !defined(EMBER_LIBEVENT_SOURCE)
.error Set EMBER_LIBEVENT_SOURCE to the prepared common Ports libevent source
.endif
.if empty(EMBER_LIBEVENT_SOURCE:tW:M/*) || \
    "${EMBER_LIBEVENT_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_LIBEVENT_SOURCE}/event.c)
.error EMBER_LIBEVENT_SOURCE must name the prepared absolute libevent source
.endif
LIBEVENT_DIST=	${EMBER_LIBEVENT_SOURCE}

CPPFLAGS+=	-DHAVE_CONFIG_H
CPPFLAGS+=	-I${LIBEVENT_DIST} -I${LIBEVENT_DIR}/include
CPPFLAGS+=	-I${LIBEVENT_DIST}/include

SHLIB_MAJOR=	5
SHLIB_MINOR=	1

WARNS?=		4

.PATH:		${LIBEVENT_DIST} ${LIBEVENT_DIST}/include
