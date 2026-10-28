#	$NetBSD: Makefile.inc,v 1.2 2016/01/14 00:38:19 christos Exp $

BINDIR=		/usr/bin
DIST=		${NETBSDSRCDIR}/external/gpl2/texinfo
.if defined(EMBER_TEXINFO_SOURCE)
.if empty(EMBER_TEXINFO_SOURCE) || !exists(${EMBER_TEXINFO_SOURCE}/configure)
.error EMBER_TEXINFO_SOURCE must name a prepared common Texinfo source
.endif
IDIST=          ${EMBER_TEXINFO_SOURCE}
GNU_SOURCE=     ${IDIST}
GNU_CONFIGURE_ARGS= --prefix=/usr --disable-perl-tools --enable-native-c-only-tools
.endif # EMBER_TEXINFO_SOURCE
