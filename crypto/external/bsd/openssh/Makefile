#	$NetBSD: Makefile,v 1.3 2020/03/03 00:43:20 christos Exp $

.include "${.CURDIR}/source.mk"
.if (defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)) || \
    make(clean) || make(cleandir)
SUBDIR=		config .WAIT lib .WAIT bin libexec
.else
SUBDIR=		lib .WAIT bin libexec
.endif

.include <bsd.subdir.mk>
