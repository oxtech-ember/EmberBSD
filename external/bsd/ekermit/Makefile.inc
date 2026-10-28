# $NetBSD: Makefile.inc,v 1.1 2014/08/08 19:30:35 apb Exp $

.include <bsd.own.mk>

.if defined(EMBER_EKERMIT_SOURCE)
.if empty(EMBER_EKERMIT_SOURCE:tW:M/*) || \
    "${EMBER_EKERMIT_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_EKERMIT_SOURCE}/main.c) || \
    !exists(${EMBER_EKERMIT_SOURCE}/kermit.c) || \
    !exists(${EMBER_EKERMIT_SOURCE}/unixio.c)
.error EMBER_EKERMIT_SOURCE must name the prepared common Ports E-Kermit source
.endif
EKERMITDIST= ${EMBER_EKERMIT_SOURCE}
.elif empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_EKERMIT_SOURCE to the prepared common Ports E-Kermit source
.endif
