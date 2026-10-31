# $NetBSD: pkgconfig.mk,v 1.1 2025/07/17 14:25:20 christos Exp $

.include <bsd.own.mk>

FILESDIR=/usr/lib/pkgconfig
.if defined(EMBER_OPENSSL_SOURCE)
OPENSSL_PRIVATE_LIBS=-lpthread
.else
OPENSSL_PRIVATE_LIBS=
.endif
.for pkg in ${PKGCONFIG}
FILES+=${pkg}.pc
FILESBUILD_${pkg}.pc=yes

${pkg}.pc: ${.CURDIR}/../../mkpc
	CPPFLAGS=${CPPFLAGS:N-DOPENSSLDIR=*:N-DENGINESDIR=*:Q} CPP=${CPP:Q} OPENSSL_PRIVATE_LIBS=${OPENSSL_PRIVATE_LIBS:Q} ${HOST_SH} ${.ALLSRC} ${OPENSSLSRC}/crypto ${.TARGET} > ${.TARGET}
.endfor
