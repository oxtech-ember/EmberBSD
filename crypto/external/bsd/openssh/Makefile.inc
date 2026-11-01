#	$NetBSD: Makefile.inc,v 1.16.4.3 2026/02/02 18:07:58 martin Exp $

WARNS?=	4

.include <bsd.own.mk>

USE_FORT?= yes	# network client/server

# Avoid increasing the binary size by not linking with libcrypto
# on static platforms.
.if ${MACHINE_ARCH} != "m68000"
WITH_OPENSSL=1
.endif

.include "${NETBSDSRCDIR}/crypto/external/bsd/openssh/source.mk"

.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
CPPFLAGS+= -DEMBERBSD_BASE_OPENSSH -DWITH_HPN
CPPFLAGS+= -I${NETBSDSRCDIR}/crypto/external/bsd/openssh/include
.endif

CPPFLAGS+=-I${SSHDIST}

.include "${NETBSDSRCDIR}/crypto/external/bsd/openssh/config.mk"

CPPFLAGS+=-D_OPENBSD_SOURCE 
CPPFLAGS+=-DHAVE_DLOPEN
CPPFLAGS+=-DHAVE_HEADER_AD
.if !defined(EMBER_OPENSSH_SOURCE) || empty(EMBER_OPENSSH_SOURCE)
CPPFLAGS+=-DHAVE_LOGIN_CAP
.endif
CPPFLAGS+=-DHAVE_STDLIB_H
.if ${HAVE_OPENSSL} < 11
CPPFLAGS+=-DOPENSSL_API_COMPAT=0x10100000L
.endif

.ifdef WITH_OPENSSL
CPPFLAGS+=-DWITH_OPENSSL
.if !defined(EMBER_OPENSSH_SOURCE) || empty(EMBER_OPENSSH_SOURCE)
CPPFLAGS+=-DENABLE_PKCS11
.endif
.endif

CPPFLAGS+=-DWITH_ZLIB

.if !defined(NOPIC)
CPPFLAGS+=-DHAVE_DLOPEN
.endif
.PATH: ${SSHDIST}
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
.PATH: ${SSHDIST}/openbsd-compat
CPPFLAGS+= -I${SSHDIST}/openbsd-compat
.endif

.if (${USE_PAM} != "no")
.if ${OPENSSH_CONFIG_ROLE:Uos} != "private-libssh"
CPPFLAGS+=-DUSE_PAM
.endif
.else	# USE_PAM == no
.if (${USE_SKEY} != "no")
CPPFLAGS+=-DSKEY
.endif
.endif	# USE_PAM == no

.if (${USE_KERBEROS} != "no")
CPPFLAGS+=-DGSSAPI
CPPFLAGS+=-DKRB5 -DHEIMDAL
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
CPPFLAGS+= ${OPENSSH_KRB5_CPPFLAGS}
.endif
.endif

.if (${USE_LDAP} != "no")
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
CPPFLAGS+=-DWITH_NETBSD_LPK
.else
CPPFLAGS+=-DWITH_LDAP_PUBKEY
.endif
.endif

CPPFLAGS+=-DX11BASE=\"/usr/X11R7\"

CPPFLAGS+=-DSUPPORT_UTMP -DSUPPORT_UTMPX
.if ${WITH_NETBSD_LIBWRAP:Uyes} != "no"
CPPFLAGS+=-DLIBWRAP
.endif

LINTFLAGS+=	-X 117	# nonportable '>>' on signed integer type
LINTFLAGS+=	-X 231	# parameter unused
LINTFLAGS+=	-X 247	# pointer cast to unrelated type
LINTFLAGS+=	-X 351	# missing header declaration

# We install libssh in /usr/lib/private so applications don't
# accidentally link against it.  Set LIBSUBDIR so libssh.so.N and
# libssh.so.N.M will be installed there there, and set PROGDPSUBDIRS so
# ssh/sshd/sshd-session/&c. will find it at link-time.
LIBSUBDIR=	private
PROGDPSUBDIRS+=	private
# EmberBSD: GCC16 -Werror=return-type on the vendored __dead do_ca_sign
# reaching its closing brace; add COPTS on the ssh-keygen build.
COPTS.ssh-keygen.c+=	-Wno-error=return-type
