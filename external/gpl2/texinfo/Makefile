#	$NetBSD: Makefile,v 1.1 2016/01/14 00:34:52 christos Exp $

.include <bsd.init.mk>
.include "Makefile.inc"

.if defined(EMBER_TEXINFO_SOURCE)
SUBDIR=         bin
MAN=            info.5 texinfo.5
TEXINFO=        info-stnd.texi texinfo.texi
INFOFLAGS=      -I${IDIST}/doc
.PATH:          ${IDIST}/man ${IDIST}/doc ${IDIST}/util

.if ${MKSHARE} != "no"
FILESDIR=       /usr/share/misc
FILES=          epsf.tex texinfo.dtd texinfo.tex
.endif

.if ${MKNLS} != "no"
.for F in cs.gmo da.gmo de.gmo de_AT.gmo eo.gmo fr.gmo he.gmo hr.gmo ja.gmo \
        nb.gmo nl.gmo no.gmo pl.gmo ro.gmo ru.gmo sv.gmo tr.gmo zh_CN.gmo zh_TW.gmo
FILES+=         ${F}
FILESDIR_${F}=  /usr/share/locale/${F:.gmo=}/LC_MESSAGES
FILESNAME_${F}= texinfo.mo
.endfor
.PATH: ${IDIST}/po
# Preserve existing locale paths when upstream provides the parent catalog.
de_AT.gmo: ${IDIST}/po/de.gmo
	cp -p ${.ALLSRC:Q} ${.TARGET:Q}
no.gmo: ${IDIST}/po/nb.gmo
	cp -p ${.ALLSRC:Q} ${.TARGET:Q}
realall: de_AT.gmo no.gmo
CLEANFILES+= de_AT.gmo no.gmo
.endif

.include <bsd.info.mk>
.include <bsd.prog.mk>
.include <bsd.subdir.mk>
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_TEXINFO_SOURCE to the prepared common Ports Texinfo source
.endif
.include <bsd.prog.mk>
.include <bsd.subdir.mk>
.endif # EMBER_TEXINFO_SOURCE
