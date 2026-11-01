#	$NetBSD: Makefile,v 1.19 2024/10/26 18:14:20 christos Exp $

PROG=	mdnsd

.include <bsd.init.mk>

.PATH:	${DIST}/mDNSPosix ${DIST}/mDNSCore ${DIST}/mDNSShared \
	${DIST}/mDNSShared/utilities ${DIST}/DSO

CPPFLAGS+=-I${DIST}/mDNSCore -I${DIST}/mDNSShared \
	-I${DIST}/mDNSShared/utilities \
	-I${DIST}/DSO -I${DIST}/ServiceRegistration \
	-DPROGRAM_NAME=\"mdnsd\" -DHAVE_SIGINFO -D_PLATFORM_HAS_STRONG_PRNG_ \
	-DPOSIX_BUILD -DMDNS_NO_STRICT=1
CFLAGS+= -fwrapv
#CPPFLAGS+=-DMDNS_DEBUGMSGS=99
SRCS=	PosixDaemon.c mDNSPosix.c mDNSUNP.c mDNS.c DNSDigest.c uDNS.c \
	DNSCommon.c uds_daemon.c mDNSDebug.c dnssd_ipc.c GenLinkedList.c \
	PlatformCommon.c ClientRequests.c dso.c dso-transport.c \
	dnssd_clientshim.c mdns_addr_tailq.c misc_utilities.c

COPTS.uds_daemon.c += -Wno-stack-protector

MAN=	mdnsd.8

CWARNFLAGS.clang+=	-Wno-unused-value
CWARNFLAGS+=		${CC_WNO_ADDRESS_OF_PACKED_MEMBER}

COPTS.DNSCommon.c+=	${CC_WNO_RETURN_LOCAL_ADDR}

.include <bsd.prog.mk>
