#	$NetBSD: Makefile,v 1.2 2016/01/13 00:18:01 christos Exp $

.include <bsd.own.mk>

GMOFILES= be.gmo ca.gmo cs.gmo da.gmo de.gmo el.gmo en@boldquot.gmo \
    en@quot.gmo es.gmo et.gmo fi.gmo fr.gmo gl.gmo id.gmo it.gmo \
    ja.gmo ko.gmo nl.gmo nn.gmo no.gmo pl.gmo pt.gmo pt_BR.gmo ro.gmo \
    ru.gmo sk.gmo sl.gmo sr.gmo sv.gmo tr.gmo uk.gmo zh_CN.gmo zh_TW.gmo 

.for F in ${GMOFILES}
FILES+=		${F}
FILESDIR_${F}=	/usr/share/locale/${F:.gmo=}/LC_MESSAGES
FILESNAME_${F}=	gettext-tools.mo
.endfor

# The current common catalog calls Norwegian Bokmal "nb"; retain the
# historical base locale path as an alias with identical compiled messages.
.if defined(EMBER_GETTEXT_SOURCE)
no.gmo: ${EMBER_GETTEXT_SOURCE}/gettext-tools/po/nb.gmo
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}

# Use the upstream quotation rule out of tree, with selected native tools.
.for _Q in en@quot en@boldquot
${_Q}.gmo: ${EMBER_GETTEXT_SOURCE}/gettext-tools/po/gettext-tools.pot \
    ${EMBER_GETTEXT_SOURCE}/gettext-tools/po/Rules-quot
	cd ${.OBJDIR:Q} && env -u MAKEOVERRIDES MAKEFLAGS= ${HOST_GMAKE:Q} -f ${EMBER_GETTEXT_SOURCE:Q}/gettext-tools/po/Rules-quot \
	    srcdir=${EMBER_GETTEXT_SOURCE:Q}/gettext-tools/po VPATH=${EMBER_GETTEXT_SOURCE:Q}/gettext-tools/po \
	    PACKAGE=ember-base CROSS_COMPILING=yes DOMAIN=gettext-tools \
	    MSGINIT=${HOST_MSGINIT:Q} MSGCONV=${HOST_MSGCONV:Q} \
	    MSGFILTER=${HOST_MSGFILTER:Q} SED=${HOST_SED:Q} SHELL=${HOST_SH:Q} \
	    ${_Q}.po-update-en
	${HOST_MSGFMT:Q} -o ${.TARGET:Q} ${.OBJDIR:Q}/${_Q}.po
.endfor
.endif

.include <bsd.files.mk>
.include <bsd.inc.mk>

.PATH: ${EMBER_GETTEXT_SOURCE}/gettext-tools/po
