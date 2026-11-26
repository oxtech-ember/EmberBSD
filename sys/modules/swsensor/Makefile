# $NetBSD: Makefile,v 1.1 2010/10/19 11:55:38 pgoyette Exp $

.include "../Makefile.inc"

.PATH:	${S}/dev/sysmon

KMOD=   swsensor

SRCS=	swsensor.c

# The sysctl node numbers are set for the sysctl tree only.
CPPFLAGS+=	${${ACTIVE_CC} == "clang" :? -Wno-unused-but-set-global :}

.include <bsd.kmodule.mk>
