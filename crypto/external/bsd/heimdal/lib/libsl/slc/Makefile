# $NetBSD: Makefile,v 1.5 2018/05/01 19:59:43 christos Exp $

.include <bsd.hostinit.mk>
.include <${.CURDIR}/../../../Makefile.inc>

.PATH:		${HEIMDIST}/lib/sl ${HEIMDIST}/lib/roken ${HEIMDIST}/lib/vers

PROG=slc

SRCS+=	slc-gram.y slc-lex.l getarg.c rand.c get_window_size.c \
	strupr.c print_version.c

YHEADER=	# defined

HOST_CPPFLAGS+= -I.			\
	-I${HEIMBASE}/include		\
	-I${HEIMBASE}/include/krb5	\
	-I${HEIMDIST}/lib/sl		\
	-I${HEIMDIST}/lib/roken		\
	-I${HEIMDIST}/lib/vers		\
	-DHAVE_CONFIG_H

HEIMDAL_PRINT_VERSION= yes
.include <${HEIMBASE}/share/mk/heimdal-version.mk>

beforedepend:
	[ -h krb5 ] || ln -sf . krb5
	ln -sf ${HEIMBASE}/include/roken.h
	ln -sf ${HEIMBASE}/include/krb5-types.h
	ln -sf ${HEIMDIST}/lib/roken/roken-common.h
	ln -sf ${HEIMDIST}/lib/roken/getarg.h

CLEANFILES+=	krb5 roken.h krb5-types.h roken-common.h getarg.h

# HOSTPROG will be set already if this is a src/tools build.
.ifndef HOSTPROG
HOSTPROG=	${PROG}
.include <bsd.hostprog.mk>
.endif
