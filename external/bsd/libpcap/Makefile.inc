# $NetBSD: Makefile.inc,v 1.5 2025/07/14 10:25:51 kre Exp $

PCAP_CONFIG_TEMPLATE := ${.PARSEDIR}/include/config.h

.if defined(EMBER_LIBPCAP_SOURCE)
.if empty(EMBER_LIBPCAP_SOURCE:tW:M/*) || \
    "${EMBER_LIBPCAP_SOURCE:tW:C,[A-Za-z0-9_./+@%=-],,g}" != "" || \
    !exists(${EMBER_LIBPCAP_SOURCE}/VERSION) || \
    !exists(${EMBER_LIBPCAP_SOURCE}/pcap/pcap.h) || \
    !exists(${EMBER_LIBPCAP_SOURCE}/pcap-bpf.c) || \
    !exists(${EMBER_LIBPCAP_SOURCE}/rpcapd/rpcapd.c)
.error EMBER_LIBPCAP_SOURCE must name the prepared common Ports libpcap source
.endif
SRCDIR := ${EMBER_LIBPCAP_SOURCE}
.elif empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_LIBPCAP_SOURCE to the prepared common Ports libpcap source
.else
SRCDIR := ${.CURDIR}
.endif

.NOPATH: config.h
config.h: ${PCAP_CONFIG_TEMPLATE} ${SRCDIR}/VERSION
	${_MKTARGET_CREATE}
	@v=$$(${TOOL_SED} -n '1p' ${SRCDIR}/VERSION); \
	    ${TOOL_SED} -e "s/^#define PACKAGE_STRING .*/#define PACKAGE_STRING \"pcap $$v\"/" \
	    -e "s/^#define PACKAGE_VERSION .*/#define PACKAGE_VERSION \"$$v\"/" \
	    < ${PCAP_CONFIG_TEMPLATE} > ${.TARGET}.tmp && mv ${.TARGET}.tmp ${.TARGET}

USE_OPENSSL ?= no

CPPFLAGS+=-DLBL_ALIGN
OPENSSLDIR=     ${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}

__sed: .USE
	@rm -f ${.TARGET}
	${TOOL_SED} \
	    -e 's,@MAN_FILE_FORMATS@,5,g' \
	    -e 's,@MAN_MISC_INFO@,7,g' \
	    -e 's,@MAN_ADMIN_COMMANDS@,8,g' \
	    -e 's,3PCAP,3,g' < ${.ALLSRC} > ${.TARGET}
