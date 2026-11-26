# SPDX-License-Identifier: BSD-2-Clause
.include <bsd.own.mk>

PROG=	ember-install-slot
BINDIR=	/usr/libexec
MAN=
SRCS=	install.c merge.c loader_files.c layout.c efi.c
SLOTDIR=${.CURDIR}/../ember-os-slot
.PATH: ${SLOTDIR}
CPPFLAGS+= -I${SLOTDIR}
.include "${NETBSDSRCDIR}/common/lib/libemberupdate/Makefile.inc"
SCRIPTS= stage-ffs
SCRIPTSNAME_stage-ffs=ember-stage-ffs
SCRIPTSDIR=/usr/libexec
LDADD+=	-lutil
DPADD+=	${LIBUTIL}
.include <bsd.prog.mk>
