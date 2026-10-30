#	$NetBSD: Makefile,v 1.1 2025/07/17 14:26:11 christos Exp $

# RCSid:
#	Id: Makefile,v 1.33 1998/11/11 11:53:53 sjg Exp
#
#	@(#) Copyright (c) 1994 Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that
#	the above copyright notice and this notice are
#	left intact.
#
#	Please send copies of changes and bug-fixes to:
#	sjg@quick.com.au
#
NOLINT=		# don't build a lint library
NOPROFILE=	# don't build a profile library
NOPICINSTALL=	# don't install _pic.a library
LIB=	legacy
USE_FORT?= yes	# cryptographic software
#DBG=-g

.include <bsd.own.mk>
.include <bsd.shlib.mk>

CPPFLAGS+= -Dlib${LIB} -I. -I${OPENSSLSRC}/crypto -I${OPENSSLSRC} -I${.CURDIR}
CPPFLAGS+= -I${OPENSSLSRC}/include -I${OPENSSLSRC}/crypto/include
CPPFLAGS+= -I${OPENSSLSRC}/crypto/asn1 -I${OPENSSLSRC}/crypto/evp
CPPFLAGS+= -I${OPENSSLSRC}/crypto/modes
CPPFLAGS+= -I${OPENSSLGENINC}
CPPFLAGS+= -I${OPENSSLSRC}/providers/common/include
CPPFLAGS+= -I${OPENSSLSRC}/providers/fips/include
CPPFLAGS+= -I${OPENSSLSRC}/providers/implementations/include
CPPFLAGS+= -I${.CURDIR}/../libdefault


CRYPTODIST=	${NETBSDSRCDIR}/crypto
OPENSSLINC=	${OPENSSLSRC}/include/openssl

.include "${NETBSDSRCDIR}/crypto/Makefile.openssl"
.PATH: ${OPENSSLSRC} ${OPENSSLINC}
.PATH: ${OPENSSLGENINC}/openssl

.if defined(EMBER_OPENSSL_SOURCE)
.include "${EMBER_OPENSSL_GENERATED}/legacy.mk"
.include "${EMBER_OPENSSL_GENERATED}/legacy-module.mk"
.else
.include "srcs.inc"
.endif

#SRCS+=legacyprov.c

AFLAGS+=-DELF

OS_VERSION!= ${HOST_SH} ${NETBSDSRCDIR}/sys/conf/osrelease.sh


SHLIB_MAJOR=0
SHLIB_MINOR=0

LIBDIR=${OSSL_MODULESDIR}

.if ${MKPIC} != "no"
.PRECIOUS: ${DESTDIR}${LIBDIR}/${LIB}.so
libinstall:: ${DESTDIR}${LIBDIR}/${LIB}.so
.else
libinstall::
.endif

.if defined(EMBER_OPENSSL_SOURCE)
VERSION_MAP= ${EMBER_OPENSSL_GENERATED}/providers/legacy.ld
.else
VERSION_MAP=	${LIB}.map
.endif

.if ${MKPICLIB} == "yes"
LIBDPLIBS+= common_pic ${.CURDIR}/../libcommon
.else
LIBDPLIBS+= common ${.CURDIR}/../libcommon
.endif
.if defined(EMBER_OPENSSL_SOURCE) && ${MKPIC} != "no" && ${MKSHLIB} != "no"
LIBDO.crypto!= cd ${.CURDIR}/../libcrypto && ${PRINTOBJDIR}
DPADD+= ${LIBDO.crypto}/libcrypto.so.17
LDADD+= ${LIBDO.crypto}/libcrypto.so.17
.else
LIBDPLIBS+= crypto ${.CURDIR}/../libcrypto
.endif

.include <bsd.lib.mk>

${DESTDIR}${LIBDIR}/${LIB}.so: lib${LIB}.so.${SHLIB_FULLVERSION}
	${_MKTARGET_INSTALL}
	${INSTALL_FILE} -o ${LIBOWN} -g ${LIBGRP} -m ${LIBMODE} \
	    ${.ALLSRC} ${.TARGET}
