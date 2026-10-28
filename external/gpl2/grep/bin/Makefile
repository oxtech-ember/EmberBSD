# Origin: EmberBSD (AI-assisted); build the common Ports GNU grep source.
.include "${.CURDIR}/../Makefile.inc"

.if defined(EMBER_GREP_SOURCE)
PROG= grep
SRCS=
MAN= grep.1
MLINKS= grep.1 egrep.1 grep.1 fgrep.1
SCRIPTS= egrep.sh fgrep.sh
TEXINFO= grep.texi
INFOFLAGS= -I${IDIST}/doc

GNU_SOURCE= ${EMBER_GREP_SOURCE}
GNU_OBJDIR= ${.OBJDIR}/gnu-build
.PATH: ${IDIST}/doc ${IDIST}/po ${GNU_OBJDIR}/doc
GNU_BUILD_TRIPLE= ${EMBER_HOST_GNU_TRIPLE}
GNU_HOST_TRIPLE= ${MACHINE_GNU_PLATFORM}
GNU_CONFIGURE_ARGS= --prefix=/usr --disable-perl-regexp
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
.if ${MKNLS} == "no"
GNU_CONFIGURE_ARGS+= --disable-nls
.else
GNU_CONFIGURE_ARGS+= --with-base-libintl
.for F in cs.gmo de.gmo el.gmo eo.gmo es.gmo et.gmo fr.gmo gl.gmo hr.gmo \
    id.gmo it.gmo ja.gmo ko.gmo nl.gmo pl.gmo pt_BR.gmo ru.gmo sl.gmo sv.gmo
FILES+= ${F}
FILESDIR_${F}= /usr/share/locale/${F:.gmo=}/LC_MESSAGES
FILESNAME_${F}= grep.mo
.endfor
FILES+= nb.gmo
FILESDIR_nb.gmo= /usr/share/locale/nb/LC_MESSAGES
FILESNAME_nb.gmo= grep.mo
LINKS+= /usr/share/locale/nb/LC_MESSAGES/grep.mo /usr/share/locale/no/LC_MESSAGES/grep.mo
LINKSMODE= ${NONBINMODE}
.endif
GNU_MAKE_DIRS= lib src doc

.include <bsd.gnutarget.mk>
.include <bsd.info.mk>
.include <bsd.prog.mk>

${GNU_OBJDIR}/src/grep: gnu-target-build
	@test -f ${.TARGET:Q}
grep: ${GNU_OBJDIR}/src/grep
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}
${GNU_OBJDIR}/doc/grep.1: gnu-target-build
	@test -f ${.TARGET:Q}
grep.1: ${GNU_OBJDIR}/doc/grep.1
	rm -f ${.TARGET:Q}
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}
.if defined(_PROGDEBUG.grep)
${_PROGDEBUG.grep}: grep __progdebug
.endif
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_GREP_SOURCE to the prepared common Ports GNU grep source
.endif
.include <bsd.prog.mk>
.endif
