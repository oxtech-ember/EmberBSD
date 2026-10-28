# $NetBSD: Makefile,v 1.1 2016/01/14 04:22:38 christos Exp $
.include <bsd.init.mk>

.if defined(EMBER_RCS_SOURCE)
PROGS= rcs ident merge
SCRIPTS= ci co rcsclean rcsdiff rcsmerge rlog rcsfreeze
.for _P in ${PROGS}
SRCS.${_P}=
.endfor
MAN= ci.1 co.1 ident.1 merge.1 rcs.1 rcsclean.1 rcsdiff.1 \
    rcsfreeze.1 rcsintro.1 rcsmerge.1 rlog.1 rcsfile.5
MLINKS= rlog.1 rcslog.1
LINKS= ${BINDIR}/rlog ${BINDIR}/rcslog

GNU_SOURCE= ${IDIST}
GNU_OBJDIR= ${.OBJDIR}/gnu-build
GNU_BUILD_TRIPLE= ${EMBER_HOST_GNU_TRIPLE}
GNU_HOST_TRIPLE= ${MACHINE_GNU_PLATFORM}
GNU_CONFIGURE_ARGS= --prefix=/usr
GNU_CONFIGURE_ENV= DIFF=/usr/bin/diff DIFF3=/usr/bin/diff3 \
    ac_cv_path_DIFF=/usr/bin/diff ac_cv_path_DIFF3=/usr/bin/diff3 \
    ac_cv_path_ED=/bin/ed rcs_cv_sane_diff3=yes
GNU_MAKE_DIRS= lib src
# Autoconf probes use intentionally non-prototype declarations; keep warnings
# visible during probes and enforce the full OS flags for actual compilation.
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}

# Preserve the OS rlog -c interface: commit IDs are shown only on request.
CPPFLAGS+= -DRCS_RLOG_EXPLICIT_COMMITID
# RCS_LOCALID names an additional Id-style keyword; the lexer accepts up to
# 32 ASCII letters, as in the previous OS RCS implementation.
.if defined(RCS_LOCALID)
CPPFLAGS+= -DLOCALID=\"${RCS_LOCALID}\"
.endif

.include <bsd.gnutarget.mk>
.include <bsd.prog.mk>

.for _P in ${PROGS} ${SCRIPTS}
${GNU_OBJDIR}/src/${_P}: gnu-target-build
	@test -f ${.TARGET:Q}
${_P}: ${GNU_OBJDIR}/src/${_P}
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}
.endfor

.for _P in ${PROGS}
.if defined(_PROGDEBUG.${_P})
${_PROGDEBUG.${_P}}: ${_P} __progdebug
.endif
.endfor
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_RCS_SOURCE to the prepared common Ports RCS source
.endif
.include <bsd.prog.mk>
.endif
