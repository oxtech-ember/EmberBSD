# Origin: EmberBSD (AI-assisted).
.include <bsd.own.mk>
.if defined(EMBER_GREP_SOURCE)
IDIST= ${EMBER_GREP_SOURCE}
.endif
BINDIR= /usr/bin
.if exists(${.CURDIR}/../../Makefile.inc)
.include "${.CURDIR}/../../Makefile.inc"
.endif
