#	$NetBSD: Makefile.inc,v 1.1 2013/11/28 22:33:43 christos Exp $

WARNS?=	1	# XXX -Wshadow -Wcast-qual -Wsign-compare

USE_FORT?=yes	# network client/server

.include <bsd.own.mk>
.if defined(EMBER_PPP_SOURCE)
.if empty(EMBER_PPP_SOURCE:tW:M/*) || \
    "${EMBER_PPP_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_PPP_SOURCE}/pppd/pppd.h) || \
    !exists(${EMBER_PPP_SOURCE}/pppd/sys-bsd.c) || \
    !exists(${EMBER_PPP_SOURCE}/chat/chat.c)
.error EMBER_PPP_SOURCE must name the prepared common Ports PPP source
.endif
DIST= ${EMBER_PPP_SOURCE}
.elif empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_PPP_SOURCE to the prepared common Ports PPP source
.endif
BINDIR=/usr/sbin

.if defined(HAVE_GCC) || defined(HAVE_LLVM)
COPTS+=	-fno-strict-aliasing
.endif
