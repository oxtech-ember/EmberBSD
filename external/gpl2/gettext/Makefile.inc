#	$NetBSD: Makefile.inc,v 1.1 2016/01/12 22:58:00 christos Exp $

.include <bsd.own.mk>

.if defined(EMBER_GETTEXT_SOURCE)
DIST=		${EMBER_GETTEXT_SOURCE}
.else
DIST=		${NETBSDSRCDIR}/external/gpl2/gettext/dist

CPPFLAGS+=	-DHAVE_CONFIG_H -I${.CURDIR} -I${.CURDIR}/../../include \
		-I${DIST}/gettext-tools/gnulib-lib \
		-DLOCALEDIR=\"/usr/share/locale\" \
		-DBISON_LOCALEDIR=\"\" \
		-DPKGDATADIR=\"\"

CWARNFLAGS.clang+=	-Wno-format-security
.endif

BINDIR?=	/usr/bin
