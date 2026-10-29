#	$NetBSD: Makefile,v 1.4 2024/09/02 16:04:42 christos Exp $

.include <bsd.init.mk>

.PATH: ${SRCDIR}

SCRIPTSDIR=/usr/bin
SCRIPTS=pcap-config

MAN=pcap-config.1

pcap-config: pcap-config.in ${SRCDIR}/VERSION
	@rm -f ${.TARGET}
	@v=$$(${TOOL_SED} -n '1p' ${SRCDIR}/VERSION); \
	    ${TOOL_SED} -e 's,@libdir@,/usr/lib,g' \
	    -e 's,@includedir@,/usr/include,g' \
	    -e 's,@PACKAGE_NAME@,pcap,g' \
	    -e 's,@prefix@,/usr,g' \
	    -e 's,@exec_prefix@,/usr,g' \
	    -e 's/@V_RPATH_OPT@/-Wl,-rpath,/g' \
	    -e 's/@RPATH@//g' \
	    -e 's,@LIBS@,,g' -e 's,@LIBS_STATIC@,,g' \
	    -e "s,@PACKAGE_VERSION@,$$v,g" < ${SRCDIR}/pcap-config.in > ${.TARGET}
	chmod a+x ${.TARGET}

CLEANFILES+=	pcap-config

NOPROG=
		
.include <bsd.prog.mk>
