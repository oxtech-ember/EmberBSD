#	$NetBSD: Makefile,v 1.4.2.1 2025/08/16 16:20:22 martin Exp $

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

.include <bsd.own.mk>

USE_FORT?=yes	# cryptographic software and network library

CWARNFLAGS.clang+=	-Wno-unused-value
# XXX: This warning seems to trigger incorrectly
CWARNFLAGS.clang+=	-Wno-atomic-alignment

LIB=	ssl
CPPFLAGS+= -Dlib${LIB} -I${OPENSSLSRC} -I${OPENSSLSRC}/crypto
CPPFLAGS+= -I${OPENSSLSRC}/include

CRYPTODIST=	${NETBSDSRCDIR}/crypto

.include "${NETBSDSRCDIR}/crypto/Makefile.openssl"
.if defined(EMBER_OPENSSL_SOURCE)
CPPFLAGS+= -I${OPENSSLGENINC}
.PATH: ${OPENSSLGENINC}/openssl
.endif
.PATH: ${OPENSSLSRC} ${OPENSSLSRC}/crypto/pqueue ${OPENSSLSRC}/include/openssl

.if defined(EMBER_OPENSSL_SOURCE)
.if !defined(OPENSSL_OBJECT_ONLY)
.include "${EMBER_OPENSSL_GENERATED}/ssl.mk"
.endif
.if !defined(OPENSSL_OBJECT_ONLY)
.include "${EMBER_OPENSSL_GENERATED}/ssl-shared.mk"
.endif
.else
.include "srcs.inc"
.endif

.if defined(EMBER_OPENSSL_SOURCE) && ${MKPIC} != "no" && ${MKSHLIB:Uyes} != "no"
# Select this build's crypto explicitly: the destination may still contain
# an older libcrypto that precedes object directories in the linker search.
LIBDO.crypto!= cd ${.CURDIR}/../libcrypto && ${PRINTOBJDIR}
DPADD+= ${LIBDO.crypto}/libcrypto.so.17
LDADD+= ${LIBDO.crypto}/libcrypto.so.17
LIBDPLIBS+=pthread ${NETBSDSRCDIR}/lib/libpthread
.elif defined(EMBER_OPENSSL_SOURCE)
LIBDPLIBS+=crypto ${.CURDIR}/../libcrypto pthread ${NETBSDSRCDIR}/lib/libpthread
.else
LIBDPLIBS+=crypto ${.CURDIR}/../libcrypto
.endif

AFLAGS+=-DELF

# This is from the include/openssl directory; see ../libcrypto/Makefile
INCS=	dtls1.h ssl.h ssl2.h ssl3.h tls1.h srtp.h async.h
INCSDIR=/usr/include/openssl

.if defined(EMBER_OPENSSL_SOURCE)
SHLIB_MAJOR= 17
SHLIB_MINOR= 0
VERSION_MAP= ${EMBER_OPENSSL_GENERATED}/libssl.ld
.else
VERSION_MAP=	ssl.map
.endif

PKGCONFIG=libssl openssl
.include "${.CURDIR}/../../pkgconfig.mk"

.if !defined(EMBER_OPENSSL_SOURCE)
GENH = \
ssl.h

.for i in ${GENH}
$i: $i.in
	${_MKTARGET_CREATE}
	${HOST_SH} ${.CURDIR}/../libcrypto/gen ${.ALLSRC} > ${.TARGET}
.endfor

.endif # !EMBER_OPENSSL_SOURCE

.include <bsd.lib.mk>
