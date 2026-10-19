#	$NetBSD$

.include "../Makefile.inc"

.PATH:	${S}/dev/pci

KMOD=	if_iwx
IOCONF=	if_iwx.ioconf

SRCS+=	if_iwx.c

CPPFLAGS+=	-DINET
COPTS+=		-ferror-limit=0

.include <bsd.kmodule.mk>
