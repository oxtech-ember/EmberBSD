# $NetBSD: Makefile,v 1.7 2021/10/22 13:25:57 roy Exp $
#

.include <bsd.own.mk>

.include "../../Makefile.inc"
HOOKSRC=	${DIST}/hooks
.PATH:		${HOOKSRC}
.include "../hook-subst.mk"

FILES=		01-test 20-resolv.conf 30-hostname
FILES+=		50-ntp.conf
FILESBUILD_30-hostname=yes

30-hostname: ${HOOKSRC}/30-hostname.in ${DHCPCD_CONFIG}
	${TOOL_SED} ${DHCPCD_HOSTNAME_SUBST} ${HOOKSRC}/30-hostname.in > ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}

FILESDIR=	/libexec/dhcpcd-hooks

.include <bsd.prog.mk>
