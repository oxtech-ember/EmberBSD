# SPDX-License-Identifier: BSD-2-Clause
.include <bsd.own.mk>

PROG=	ember-os-slot
BINDIR=	/usr/libexec
MAN=
SRCS=	main.c layout.c efi.c
.include "${NETBSDSRCDIR}/common/lib/libemberupdate/Makefile.inc"
LDADD+=	-lutil
DPADD+=	${LIBUTIL}
.include <bsd.prog.mk>
