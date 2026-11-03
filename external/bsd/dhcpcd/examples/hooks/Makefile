# $NetBSD: Makefile,v 1.4 2021/10/22 13:25:57 roy Exp $
#

.include <bsd.own.mk>

.include "../../Makefile.inc"
.PATH:			${DIST}/hooks

.if ${MKSHARE} != "no"
FILES=			10-wpa_supplicant 15-timezone 29-lookup-hostname

.if ${MKYP} != "no"
FILES+=			50-ypbind
FILESBUILD_50-ypbind=	yes
CLEANFILES+=		50-ypbind
# The prepared source has a template; NetBSD ypbind reads the servers
# from /var/yp/DOMAIN.ypservers.
50-ypbind: ${DIST}/hooks/50-ypbind.in
	${TOOL_SED} -e 's:@YPDOMAIN_DIR@:/var/yp:g' \
	    -e 's:@YPDOMAIN_SUFFIX@:.ypservers:g' ${.ALLSRC} > ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}
.endif

FILESDIR=		/usr/share/examples/dhcpcd/hooks
.endif

.include <bsd.prog.mk>
