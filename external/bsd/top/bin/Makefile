#	$NetBSD: Makefile,v 1.13 2023/06/03 09:09:05 lukem Exp $

.include <bsd.own.mk>

PROG=	top
BINDIR=	/usr/bin

.if defined(EMBER_TOP_SOURCE)
.if empty(EMBER_TOP_SOURCE:tW:M/*) || \
    "${EMBER_TOP_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_TOP_SOURCE}/top.c) || \
    !exists(${EMBER_TOP_SOURCE}/machine/m_netbsd.c)
.error EMBER_TOP_SOURCE must name the prepared common Ports top source
.endif
TOPDIR= ${EMBER_TOP_SOURCE}
.elif empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_TOP_SOURCE to the prepared common Ports top source
.endif
.PATH:	${TOPDIR} ${TOPDIR}/machine

WARNS=	4
CWARNFLAGS+=	-Wno-missing-noreturn

CPPFLAGS+=-I${.CURDIR} -I${TOPDIR} -I. -D_KERNTYPES
SRCS=	color.c commands.c display.c hash.c screen.c \
	top.c username.c utils.c version.c m_netbsd.c
DPSRCS+=sigdesc.h config.h

LDADD+=	-lterminfo -lm -lkvm -lutil
DPADD+=	${LIBTERMINFO} ${LIBM} ${LIBKVM} ${LIBUTIL}

sigdesc.h: ${TOPDIR}/sigconv.awk ${DESTDIR}/usr/include/sys/signal.h
	${_MKTARGET_CREATE}
	${TOOL_AWK} -f ${TOPDIR}/sigconv.awk \
	    ${DESTDIR}/usr/include/sys/signal.h > ${.TARGET}
CLEANFILES+= sigdesc.h top.1 config.h

top.1: top.1.in
	${TOOL_SED} -e s/@DEFAULT_TOPN@/-1/ \
		-e s/@DEFAULT_DELAY@/5/ \
		-e s/@HAVE_GETOPT_LONG@/1/ \
		-e s/@ENABLE_KILL@/1/ \
		-e s/@MAN_SUPPLEMENT@// < $? > $@

commands.c: sigdesc.h

config.h: config.h.in
	${TOOL_SED} -e s/@MACHINE@/${MACHINE}/ < $? > $@

COPTS.display.c = -Wno-format-nonliteral

COPTS+=		-fcommon

.include <bsd.prog.mk>
