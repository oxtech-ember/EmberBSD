# Origin: EmberBSD external ucom rump build integration, 2026-10-10.
#	$NetBSD: Makefile,v 1.7 2016/01/26 23:41:15 pooka Exp $
#

.PATH:	${.CURDIR}/../../../../dev/usb ${.CURDIR}/../../../../kern

LIB=	rumpdev_ucom
COMMENT=USB serial driver

IOCONF= UCOM.ioconf

SRCS=	ucom.c ucom_transport_core.c ucom_transport_rx.c uplcom.c u3g.c

SRCS+=	umoxa_frame.c umoxa_g2_txq.c
SRCS+=	ucom_at_usb.c

.include <bsd.lib.mk>
.include <bsd.klinks.mk>
