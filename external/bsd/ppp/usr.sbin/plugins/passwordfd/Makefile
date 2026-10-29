#	$NetBSD: Makefile,v 1.2 2023/11/15 22:11:17 christos Exp $

NOLINT=	# defined

.include "../Makefile.inc"

.PATH: ${PPPD}/plugins

LIB=passwordfd
SRCS=passwordfd.c
CPPFLAGS+=-I${DIST} -I${NETBSDSRCDIR}/external/bsd/ppp/include

LIBPREFIX=
SHLIB_FULLVERSION=

.include <bsd.lib.mk>
