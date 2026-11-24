# $NetBSD: Makefile,v 1.1 2023/07/05 00:26:38 gutteridge Exp $

.include <bsd.own.mk>

.include "../../Makefile.inc"
.PATH:			${DIST}/src
.include "../../template-subst.mk"

.if ${MKSHARE} != "no"
FILES=			dhcpcd.conf

FILESDIR=		${DHCPCD_DATADIR}/dhcpcd/
.endif

.include <bsd.prog.mk>
