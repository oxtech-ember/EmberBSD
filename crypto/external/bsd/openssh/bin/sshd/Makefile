#	$NetBSD: Makefile,v 1.28 2024/07/08 22:33:43 christos Exp $

.include <bsd.own.mk>

.if !defined(EMBER_OPENSSH_SOURCE) || empty(EMBER_OPENSSH_SOURCE)
USE_LDAP=	no
.endif
PROG=	sshd
MAN=	sshd.8 sshd_config.5 moduli.5

BINDIR=	/usr/sbin

SRCS=	sshd.c sshpty.c srclimit.c servconf.c auth2-methods.c \
	groupaccess.c

.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
SRCS+=	platform-listen.c
.endif

LDADD+=	-lutil
DPADD+=	${LIBUTIL}

.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE) && \
    ${WITH_NETBSD_LIBWRAP:Uyes} != "no"
LDADD+=	-lwrap
DPADD+=	${LIBWRAP}
.endif

.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE) && (${USE_LDAP} != "no")
SRCS+=	ldapauth.c
LDADD+=	${LIBLDAP_LDADD}
DPADD+=	${LIBLDAP_DPADD}
.endif

.include <bsd.prog.mk>
