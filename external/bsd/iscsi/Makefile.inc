#	$NetBSD: Makefile.inc,v 1.3 2009/06/25 13:47:09 agc Exp $
#	from: @(#)Makefile.inc	8.1 (Berkeley) 6/6/93

BINDIR?=	/usr/sbin
WARNS?=		4

.if defined(EMBER_ISCSI_SOURCE)
.if empty(EMBER_ISCSI_SOURCE:tW:M/*) || \
    "${EMBER_ISCSI_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_ISCSI_SOURCE}/include/iscsi.h) || \
    !exists(${EMBER_ISCSI_SOURCE}/src/lib/crc32.c) || \
    !exists(${EMBER_ISCSI_SOURCE}/src/initiator/iscsi-initiator.c) || \
    !exists(${EMBER_ISCSI_SOURCE}/src/target/iscsi-target.c)
.error EMBER_ISCSI_SOURCE must name the prepared common Ports iSCSI source
.endif
ISCSIDIST= ${EMBER_ISCSI_SOURCE}
.elif empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_ISCSI_SOURCE to the prepared common Ports iSCSI source
.else
ISCSIDIST= ${.CURDIR}
.endif
