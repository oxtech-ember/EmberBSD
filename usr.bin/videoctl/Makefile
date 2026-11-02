# $NetBSD: Makefile,v 1.1 2010/12/26 10:37:14 jmcneill Exp $

PROG=	videoctl
SRCS=	videoctl.c video_capture_bsd.c video_capture.c
LDADD+=	-lutil
DPADD+=	${LIBUTIL}

.include <bsd.prog.mk>
