# $NetBSD: Makefile.inc,v 1.1 2018/09/08 14:11:10 christos Exp $

.include <bsd.own.mk>

LIBNV_DIR=	${NETBSDSRCDIR}/sys/external/bsd/libnv
LIBNV_DIST=	${LIBNV_DIR}/dist

#CPPFLAGS+=	-DWITH_MSGIO
# EmberBSD: drop -D_VA_LIST_DEFINED: GCC16's stdarg.h treats it as
# "va_list already provided" and skips the typedef, breaking userspace.
CPPFLAGS+=	-D__FBSDID\(x\)=
CPPFLAGS+=	-I${LIBNV_DIST}

.PATH:		${LIBNV_DIST}
