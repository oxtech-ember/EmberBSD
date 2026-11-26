# $NetBSD: Makefile,v 1.4 2021/11/07 18:05:36 martin Exp $

NOMAN=		# defined
WARNS=4

.include <bsd.own.mk>

TESTSDIR=	${TESTSBASE}/lib/libc/gen/posix_spawn

TESTS_C=	t_spawn
TESTS_C+=	t_fileactions
TESTS_C+=	t_spawnattr

SRCS.t_spawn += t_spawn fa_spawn_utils.c
SRCS.t_fileactions += t_fileactions fa_spawn_utils.c

BINDIR=		${TESTSDIR}
SCRIPTSDIR=	${TESTSDIR}

PROGS=		h_fileactions
PROGS+=		h_spawn
PROGS+=		h_spawnattr
SCRIPTS=	h_nonexec h_zero

# The build has the tools awk, not dd. SCRIPTS installs h_zero with its mode.
h_zero:
	${TOOL_AWK} 'BEGIN { for (i = 0; i < 2048; i++) printf "%c", 0 }' > ${.TARGET}

CLEANFILES+=	h_nonexec h_zero

.include <bsd.test.mk>
