# $NetBSD: Makefile,v 1.1 2024/10/18 23:22:52 christos Exp $

.include <bsd.own.mk>

TESTSDIR=	${TESTSBASE}/usr.sbin/makefs
TESTS_SH=	t_makefs

.if defined(EMBER_LITTLEFS_SOURCE) && !empty(EMBER_LITTLEFS_SOURCE)
TESTS_SH+=	t_littlefs
PROGS+=	h_littlefs_check
BINDIR=${TESTSDIR}
SRCS.h_littlefs_check=	h_littlefs_check.c \
	${EMBER_LITTLEFS_SOURCE}/lfs.c ${EMBER_LITTLEFS_SOURCE}/lfs_util.c
CPPFLAGS.h_littlefs_check+=	-I${EMBER_LITTLEFS_SOURCE} \
	-I${NETBSDSRCDIR}/sys/fs/littlefs
.endif

.include <bsd.test.mk>
