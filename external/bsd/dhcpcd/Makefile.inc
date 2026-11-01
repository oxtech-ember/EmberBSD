# $NetBSD: Makefile.inc,v 1.5 2021/10/22 13:25:57 roy Exp $

SRCDIR=		${NETBSDSRCDIR}/external/bsd/dhcpcd
.if defined(EMBER_DHCPCD_SOURCE)
DIST?=		${EMBER_DHCPCD_SOURCE}
.if "${DIST}" != "${EMBER_DHCPCD_SOURCE}"
.error DIST must match the prepared common dhcpcd source
.endif
.elif make(obj) && empty(.TARGETS:Nobj)
DIST?=		${SRCDIR}/dist
.else
.error Set EMBER_DHCPCD_SOURCE to the prepared common Ports source
.endif
