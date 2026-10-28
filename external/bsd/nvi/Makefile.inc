#	$NetBSD: Makefile.inc,v 1.6 2018/06/25 18:00:34 kamil Exp $

.include <bsd.own.mk>

SANITIZER_RENAME_SYMBOL+=	regcomp regerror regexec regfree

# The native build uses the prepared common Ports nvi source.
.if defined(EMBER_NVI_SOURCE)
.if empty(EMBER_NVI_SOURCE:tW:M/*) || \
    "${EMBER_NVI_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_NVI_SOURCE}/common/main.c) || \
    !exists(${EMBER_NVI_SOURCE}/dist/ember-gen-headers.sh)
.error EMBER_NVI_SOURCE must name the prepared common Ports nvi source
.endif
DIST= ${EMBER_NVI_SOURCE}
.PATH: ${DIST}
.elif empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_NVI_SOURCE to the prepared common Ports nvi source
.endif
BINDIR=/usr/bin

VERSION=1.81.6-2013-11-20nb4
