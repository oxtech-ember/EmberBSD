#	$NetBSD: Makefile,v 1.17 2024/06/30 17:46:22 christos Exp $

.include <bsd.hostinit.mk>
.include "${.PARSEDIR}/../host-bfd.mk"

HOSTPROGNAME=	${MACHINE_GNU_PLATFORM}-dbsym

HOST_SRCDIR=    external/gpl3/${EXTERNAL_BINUTILS_SUBDIR}/usr.sbin/dbsym

.if !defined(EMBER_HOST_BFD_CONFIG)
TOOLCHAINOBJ!=	cd ${.CURDIR}/../binutils && ${PRINTOBJDIR}
BFDDIR?=	${TOOLCHAINOBJ}/build/bfd
SFRAMEDIR?=	${TOOLCHAINOBJ}/build/libsframe
IBERTYDIR?=	${TOOLCHAINOBJ}/build/libiberty
.endif

.include "${.CURDIR}/../Makefile.host"

.if !defined(EMBER_HOST_BFD_CONFIG)
LDMAKEFILE=${TOOLCHAINOBJ}/build/ld/Makefile
.if exists(${LDMAKEFILE})
HAVE_LIBDL!=	grep -- -ldl ${LDMAKEFILE} || echo no
.else
HAVE_LIBDL=	no
.endif

.if ${HAVE_LIBDL} != "no"
LDADD+=	-ldl
.endif

.endif # !EMBER_HOST_BFD_CONFIG
