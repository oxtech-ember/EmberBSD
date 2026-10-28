#	$NetBSD: Makefile.inc,v 1.3 2024/09/15 16:06:41 christos Exp $

WARNS=4

.include <bsd.own.mk>

BINDIR?= /usr/bin

# Byacc source is prepared from the complete common Ports recipe.
.if !defined(EMBER_BYACC_SOURCE)
.error Set EMBER_BYACC_SOURCE to the prepared common Ports byacc source
.endif
.if empty(EMBER_BYACC_SOURCE:tW:M/*) || \
    "${EMBER_BYACC_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_BYACC_SOURCE}/VERSION) || \
    !exists(${EMBER_BYACC_SOURCE}/main.c) || \
    !exists(${EMBER_BYACC_SOURCE}/btyaccpar.c)
.error EMBER_BYACC_SOURCE must name the prepared absolute byacc source
.endif
IDIST=	${EMBER_BYACC_SOURCE}
YYPATCH != cat ${IDIST}/VERSION

CPPFLAGS+= -DHAVE_CONFIG_H -I${.CURDIR}/../include -I${IDIST}
CPPFLAGS+= -DYYPATCH=${YYPATCH}
CWARNFLAGS+=	-Wno-missing-noreturn

.PATH: ${IDIST}
