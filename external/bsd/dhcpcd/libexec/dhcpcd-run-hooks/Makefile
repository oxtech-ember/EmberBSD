# $NetBSD: Makefile,v 1.2 2021/10/22 13:25:58 roy Exp $
#

.include <bsd.own.mk>

.include "../../Makefile.inc"
HOOKSRC=	${DIST}/hooks
.PATH:		${HOOKSRC}
MAN=		dhcpcd-run-hooks.8
DHCPCD_MANSRC=	${HOOKSRC}
.include "../../template-subst.mk"

SCRIPTS=	dhcpcd-run-hooks
SCRIPTSDIR=	/libexec
CLEANFILES+=	dhcpcd-run-hooks dhcpcd-run-hooks.tmp

dhcpcd-run-hooks: ${HOOKSRC}/dhcpcd-run-hooks.in ${DHCPCD_CONFIG}
	${TOOL_SED} ${DHCPCD_SUBST} ${HOOKSRC}/dhcpcd-run-hooks.in > ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}

.include <bsd.prog.mk>
