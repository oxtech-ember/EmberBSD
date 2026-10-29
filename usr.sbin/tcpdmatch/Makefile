#	$NetBSD: Makefile,v 1.13 2021/03/07 15:09:13 christos Exp $
#

WARNS?=	1	# XXX: many issues in lib/libwrap to address first

.include <bsd.own.mk>
.include "${NETBSDSRCDIR}/lib/libwrap/Makefile.source"

PROG=	tcpdmatch
SRCS=	tcpdmatch.c fakelog.c inetcf.c scaffold.c percent_m.c
MAN=	tcpdmatch.8
CPPFLAGS+= -DSYS_ERRLIST_DEFINED
LDADD=	-lwrap -lblocklist
DPADD=	${LIBWRAP} ${LIBBLOCKLIST}

.include "${NETBSDSRCDIR}/lib/libwrap/Makefile.cflags"

.include <bsd.prog.mk>
