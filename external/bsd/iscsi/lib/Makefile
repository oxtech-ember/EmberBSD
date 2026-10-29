# $NetBSD: Makefile,v 1.7 2025/07/05 06:50:12 mlelstv Exp $

.include "${.CURDIR}/../Makefile.inc"

.include <bsd.own.mk>

USE_FORT?= yes	# network protocol library 

LIB= iscsi
SRCS= conffile.c disk.c util.c parameters.c protocol.c storage.c
SRCS+= netmask.c md5c.c md5hl.c crc32.c uuid.c
SRCS+= initiator.c target.c 
CPPFLAGS+= -DCONFIG_ISCSI_DEBUG -DHAVE_CONFIG_H
CPPFLAGS+= -I${ISCSIDIST}/include
CPPFLAGS+= -pthread
LDFLAGS+= -pthread
MAN= libiscsi.3
WARNS=4

.if !defined(EMBER_ISCSI_SOURCE) && empty(.MAKEFLAGS:M-V*) && make(includes)
.error Set EMBER_ISCSI_SOURCE to the prepared common Ports iSCSI source
.endif

INCS+= iscsi.h
INCSDIR=/usr/include

.PATH:	${ISCSIDIST}/src/lib ${ISCSIDIST}/include

LIBDPLIBS+=      pthread  ${.CURDIR}/../../../../lib/libpthread

COPTS.target.c+=	${CC_WNO_FORMAT_TRUNCATION}

.include <bsd.lib.mk>
