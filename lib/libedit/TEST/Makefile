# $NetBSD: Makefile,v 1.8 2017/10/15 18:59:00 abhinav Exp $

NOMAN=1
PROGS=wtc1 test_filecompletion
.include "${.CURDIR}/../source.mk"
CPPFLAGS=-I${.CURDIR}/.. -I${LIBEDITDIR}
LDADD+=-ledit -ltermlib
DPADD+=${LIBEDIT} ${LIBTERMLIB}

.ifdef DEBUG
CPPFLAGS+=-DDEBUG
.endif

.include <bsd.prog.mk>
