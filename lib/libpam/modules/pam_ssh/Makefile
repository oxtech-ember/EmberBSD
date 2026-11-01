# $NetBSD: Makefile,v 1.14.2.1 2026/01/22 19:51:54 martin Exp $
# PAM module for SSH
# $FreeBSD: src/lib/libpam/modules/pam_ssh/Makefile,v 1.18 2004/08/06 07:27:04 cperciva Exp $

NOLINT=		# don't build a lint library
NOPROFILE=	# don't build a profile library
NOPICINSTALL=	# don't install _pic.a library

.include <bsd.own.mk>

.include "${NETBSDSRCDIR}/crypto/external/bsd/openssh/source.mk"
SSHSRC=	${SSHDIST}
.if "${SSHSRC}" != "${SSHDIST}"
.error SSHSRC must match the prepared common OpenSSH source
.endif

.PATH: ${SSHSRC}
LIB=	pam_ssh
MAN=	pam_ssh.8
SRCS=	pam_ssh.c

CPPFLAGS+= -I${SSHSRC}

.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
OPENSSH_CONFIG_ROLE=private-libssh
.endif
.include "${NETBSDSRCDIR}/crypto/external/bsd/openssh/config.mk"
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
CPPFLAGS+= -include ${OPENSSH_CONFIG_HEADER}
.endif

LIBDPLIBS+=	ssh	${NETBSDSRCDIR}/crypto/external/bsd/openssh/lib \
		crypt	${.CURDIR}/../../../libcrypt \
		crypto	${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}/lib/libcrypto

# libssh is private.
#
# XXX This should be obtained through LIBDPLIBS.
LIBDPSUBDIRS+=	private

.include "${.CURDIR}/../mod.mk"
