#	$NetBSD: Makefile,v 1.5 2024/02/08 02:53:53 kre Exp $
#	@(#)Makefile	8.1 (Berkeley) 6/6/93

PROG=	touch
.if !defined(HOSTPROG)
LDADD+=	-lutil
DPADD+=	${LIBUTIL}
.endif
LDADD+=	-lm

.include <bsd.prog.mk>
