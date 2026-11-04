#	$NetBSD: Makefile,v 1.4.12.2 2026/06/16 08:55:53 martin Exp $

.include <bsd.init.mk>

LIB=		expat

.PATH:	${EXPATSRCDIR}/lib
SRCS=	random_arc4random.c random_arc4random_buf.c random_dev_urandom.c random_getentropy.c random_getrandom.c xcs.c xmlparse.c xmlrole.c xmltok.c

INCS=		expat.h expat_config.h expat_external.h
INCSDIR=	/usr/include

CPPFLAGS+=		-I${EXPATSRCDIR} -I${EXPATSRCDIR}/lib -I${.CURDIR}
CPPFLAGS+=		-DHAVE_EXPAT_CONFIG_H
COPTS.xcs.c=		-fvisibility=hidden
COPTS.xmlparse.c=	-Wno-error # macppc
COPTS.xmlrole.c=	-Wno-error # macppc
COPTS.xmltok.c=		-Wno-error # macppc

FILES+=			expat.pc
FILESOWN_expat.pc=	${BINOWN}
FILESGRP_expat.pc=	${BINGRP}
FILESMODE_expat.pc=	${NONBINMODE}
FILESDIR_expat.pc=	/usr/lib/pkgconfig

CLEANFILES+=expat.pc

.include <bsd.lib.mk>

gaga:
	@echo MKLINT=${MKLINT}
	@echo LSRCS=${LSRCS}
	@echo LOBJS=${LOBJS}

all: expat.pc
dependall: all

expat.pc: expat_config.h expat.pc.in ${EXPATSRCDIR}/lib/expat.h
	@(V=$$( (echo '#include <expat.h>'; echo 'XML_MAJOR_VERSION.XML_MINOR_VERSION.XML_MICRO_VERSION') | \
	    ${CC} ${CPPFLAGS} -E -I${EXPATSRCDIR}/lib - | ${TOOL_SED} -n '$$s/[[:space:]]//gp') && \
	    ${TOOL_SED} -e s/@VERSION@/$$V/ < ${.CURDIR}/expat.pc.in \
	    > ${.TARGET})

