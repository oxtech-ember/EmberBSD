# $NetBSD: Makefile,v 1.3 2023/06/20 17:23:03 christos Exp $

LIBISPRIVATE=	yes
NOLINKLIB=	# defined
NOPIC=		# defined
NOPROFILE=	# defined

.include <bsd.own.mk>
.include <${.CURDIR}/../../Makefile.inc>

.PATH:		${HEIMDIST}/lib/vers

LIB=		vers

includes: print_version.h

SRCS=		print_version.c
 
HOST_CPPFLAGS+=	-I${HEIMBASE}/include -DHAVE_CONFIG_H

HEIMDAL_PRINT_VERSION= yes
.include <${HEIMBASE}/share/mk/heimdal-version.mk>
 
.include <${HEIMBASE}/Makefile.rules.inc>
.include <bsd.lib.mk>
