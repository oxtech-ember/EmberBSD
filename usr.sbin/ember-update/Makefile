# SPDX-License-Identifier: BSD-2-Clause
PROG=	ember-update
SRCS=	main.c handoff.c state.c slots.c release.c delivery.c catalog.c package.c store.c desktop.c verify.c local.c
MAN=	ember-update.8
LDADD+=	-lfetch -lssl -lcrypto -lz
DPADD+=	${LIBFETCH} ${LIBSSL} ${LIBCRYPTO} ${LIBZ}

.include <bsd.own.mk>
.include "${NETBSDSRCDIR}/common/lib/libemberupdate/Makefile.inc"
.include <bsd.prog.mk>
