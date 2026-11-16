# $NetBSD: Makefile.inc,v 1.1 2016/01/14 04:22:38 christos Exp $

.include <bsd.own.mk>

BINDIR=/usr/bin
DIST=${NETBSDSRCDIR}/external/gpl2/rcs
.if defined(EMBER_RCS_SOURCE)
REPRO_EXTERNAL_SOURCE_VARS+= EMBER_RCS_SOURCE
IDIST=${EMBER_RCS_SOURCE}

.PATH: ${IDIST}/src ${IDIST}/man ${DIST}/man
.endif
