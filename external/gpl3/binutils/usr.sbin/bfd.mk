# $NetBSD: bfd.mk,v 1.1 2024/07/04 02:20:03 christos Exp $

BFDSUBDIR=      lib

.if defined(HOSTPROG) && defined(EMBER_HOST_BFD_CONFIG)
.include "${NETBSDSRCDIR}/tools/host-bfd.mk"
CPPFLAGS+=	${EMBER_HOST_BFD_CPPFLAGS}
DPADD+=		${EMBER_HOST_BFD_DPADD}
LDADD+=		${EMBER_HOST_BFD_LDADD}
.else
CPPFLAGS+=	-I${DIST}/bfd -I${DIST}/binutils -I${DIST}/include

.if !defined(HOSTPROG)
PROGDPLIBS+=	bfd	${TOP}/${BFDSUBDIR}/libbfd
PROGDPLIBS+=	sframe	${TOP}/${BFDSUBDIR}/libsframe
PROGDPLIBS+=	iberty	${TOP}/${BFDSUBDIR}/libiberty
.else
CPPFLAGS+=	-I${BFDDIR}
DPADD+=		${BFDDIR}/.libs/libbfd.a
LDADD+=		-L${BFDDIR}/.libs -lbfd
DPADD+=		${SFRAMEDIR}/.libs/libsframe.a
LDADD+=		-L${SFRAMEDIR}/.libs -lsframe
DPADD+=		${IBERTYDIR}/libiberty.a
LDADD+=		-L${IBERTYDIR} -liberty
.endif

.endif # common host BFD

.include <bsd.prog.mk>

.ifndef HOSTPROG
.include	"${TOP}/${BFDSUBDIR}/libbfd/arch/${BINUTILS_MACHINE_ARCH}/defs.mk"
CPPFLAGS+=	-I${TOP}/${BFDSUBDIR}/libbfd/arch/${BINUTILS_MACHINE_ARCH} ${G_INCLUDES}
LDADD+=		-lintl
DPADD+=		${LIBINTL}
.endif # HOSTPROG

.if !defined(HOSTPROG) || !defined(EMBER_HOST_BFD_CONFIG)
LDADD+=         -lz
# No DPADD because we don't know the format and we didn't build it.
.endif
