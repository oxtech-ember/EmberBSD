# $NetBSD: Makefile,v 1.2 2021/10/22 13:25:58 roy Exp $
#

.include <bsd.own.mk>

.include "../../Makefile.inc"
HOOKSRC=	${DIST}/hooks
.PATH:		${HOOKSRC}
.include "../hook-subst.mk"

SCRIPTS=	dhcpcd-run-hooks
SCRIPTSDIR=	/libexec

dhcpcd-run-hooks: ${HOOKSRC}/dhcpcd-run-hooks.in ${DHCPCD_CONFIG}
	${TOOL_SED} ${DHCPCD_HOOK_SUBST} ${HOOKSRC}/dhcpcd-run-hooks.in > ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}

MAN=		dhcpcd-run-hooks.8

.include <bsd.prog.mk>
