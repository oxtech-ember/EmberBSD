#	$NetBSD$

.include "../Makefile.inc"

.PATH:	${S}/dev/pci

KMOD=	if_iwx

SRCS+=	if_iwx.c ioconf.c

CPPFLAGS+=	-DINET
COPTS+=		-ferror-limit=0

.include <bsd.kmodule.mk>
