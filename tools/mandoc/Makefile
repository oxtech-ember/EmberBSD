#	$NetBSD: Makefile,v 1.16 2025/06/19 17:38:54 gutteridge Exp $

HOSTPROGNAME=	${_TOOL_PREFIX}mandoc
HOST_SRCDIR=	external/bsd/mdocml/bin/mandoc

.include <bsd.hostinit.mk>

NETBSD_MINOR!=	${HOST_SH} ${NETBSDSRCDIR}/sys/conf/osrelease.sh -n
NETBSD_OSNAME=	"NetBSD ${NETBSD_MINOR}"

# Configure probes need the host SDK flags, before mandoc-only definitions.
CONFIGURE_ENV:=	CC=${HOST_CC:Q} CFLAGS=${HOST_CFLAGS:Q} \
		CPPFLAGS=${HOST_CPPFLAGS:Q} LDFLAGS=${HOST_LDFLAGS:Q}

HOST_CPPFLAGS+=	-DOSNAME=${NETBSD_OSNAME:Q} -DHAVE_CONFIG_H -I.

DPSRCS+=	config.h

CLEANFILES+=	config.log config.h Makefile.local

config.h: configure 
	SRCDIR=${DISTDIR} ${CONFIGURE_ENV} ${HOST_SH} ${DISTDIR}/configure

.include "${.CURDIR}/../Makefile.host"
