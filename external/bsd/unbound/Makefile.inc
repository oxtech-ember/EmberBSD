# $NetBSD: Makefile.inc,v 1.6 2025/07/14 10:25:52 kre Exp $

USE_FORT?= yes  # network client/server
UNBOUND_OSDIR:=	${.PARSEDIR}
UNBOUND_GENERATED_DIR= ${.OBJDIR}/unbound-generated
.if !defined(EMBER_UNBOUND_SOURCE) && !(make(obj) && empty(.TARGETS:Nobj))
.error Set EMBER_UNBOUND_SOURCE to the prepared common Ports source
.endif
.if defined(EMBER_UNBOUND_SOURCE)
UNBOUND=	${EMBER_UNBOUND_SOURCE}
.if make(obj) && empty(.TARGETS:Nobj)
UNBOUND_VERSION=	0.0.0
.else
UNBOUND_VERSION!= sed -n "s/^PACKAGE_VERSION='\([^']*\)'/\1/p" ${UNBOUND}/configure
.if empty(UNBOUND_VERSION)
.error Cannot determine version of the prepared common Unbound source
.endif
.endif
UNBOUND_CPPFLAGS= -I${UNBOUND_GENERATED_DIR} -I${UNBOUND} -I${UNBOUND_OSDIR}/include
UNBOUND_VERSION_WORDS= ${UNBOUND_VERSION:S/./ /g}
DPSRCS+= ${UNBOUND_GENERATED_DIR}/config.h ${UNBOUND_GENERATED_DIR}/libunbound/unbound.h
CLEANFILES+= ${UNBOUND_GENERATED_DIR}/config.h ${UNBOUND_GENERATED_DIR}/libunbound/unbound.h
${UNBOUND_GENERATED_DIR}/config.h: ${UNBOUND_OSDIR}/include/config.h ${UNBOUND}/configure
	mkdir -p ${UNBOUND_GENERATED_DIR}
	sed -e 's/^#define PACKAGE_STRING .*/#define PACKAGE_STRING "unbound ${UNBOUND_VERSION}"/' \
	    -e 's/^#define PACKAGE_VERSION .*/#define PACKAGE_VERSION "${UNBOUND_VERSION}"/' \
	    -e 's/^#define RSRC_PACKAGE_VERSION .*/#define RSRC_PACKAGE_VERSION ${UNBOUND_VERSION:S/./,/g},0/' \
	    ${UNBOUND_OSDIR}/include/config.h > ${.TARGET}
${UNBOUND_GENERATED_DIR}/libunbound/unbound.h: ${UNBOUND}/libunbound/unbound.h ${UNBOUND}/configure
	mkdir -p ${UNBOUND_GENERATED_DIR}/libunbound
	sed -e 's/@UNBOUND_VERSION_MAJOR@/${UNBOUND_VERSION_WORDS:[1]}/g' \
	    -e 's/@UNBOUND_VERSION_MINOR@/${UNBOUND_VERSION_WORDS:[2]}/g' \
	    -e 's/@UNBOUND_VERSION_MICRO@/${UNBOUND_VERSION_WORDS:[3]}/g' \
	    ${UNBOUND}/libunbound/unbound.h > ${.TARGET}
.else
UNBOUND=	${UNBOUND_OSDIR}/dist
UNBOUND_CPPFLAGS= -I${UNBOUND} -I${UNBOUND_OSDIR}/include
.endif

.PATH: ${UNBOUND}/doc

DPLIBS+= ssl ${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}/lib/libssl
DPLIBS+= crypto ${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}/lib/libcrypto
DPLIBS+= crypt ${NETBSDSRCDIR}/lib/libcrypt
DPLIBS+= util ${NETBSDSRCDIR}/lib/libutil
DPLIBS+= pthread ${NETBSDSRCDIR}/lib/libpthread
DPLIBS+= event ${NETBSDSRCDIR}/external/bsd/libevent/lib/libevent

__subst: .USE
	${TOOL_SED} \
	    -e s,@ub_conf_dir@,/etc/unbound, \
	    -e s,@ub_conf_file@,/etc/unbound/unbound.conf, \
	    -e s,@UNBOUND_CHROOT_DIR@,/var/chroot/unbound, \
	    -e s,@UNBOUND_PIDFILE@,/var/run/unbound.pid, \
	    -e s,@UNBOUND_ROOTCERT_FILE@,/etc/unbound/icannbundle.pem, \
	    -e s,@UNBOUND_ROOTKEY_FILE@,/etc/unbound/root.key, \
	    -e s,@UNBOUND_RUN_DIR@,/var/chroot/unbound/etc/unbound, \
	    -e s,@UNBOUND_SHARE_DIR@,/etc/unbound, \
	    -e s,@UNBOUND_USERNAME@,_unbound, < ${.ALLSRC} > ${.TARGET}
