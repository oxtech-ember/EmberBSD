# $NetBSD: Makefile,v 1.2 2023/07/05 22:42:46 riastradh Exp $

LIB=		h_helper_dso1
SRCS=		h_helper_dso1.c

# The TLS callback only sets its variable.
COPTS.h_helper_dso1.c+=	${${ACTIVE_CC} == "clang":? -Wno-unused-but-set-global :}

.include "../helper.mk"
