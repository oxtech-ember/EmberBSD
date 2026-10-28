#	$NetBSD: Makefile,v 1.1 2016/01/14 00:34:52 christos Exp $

.include <bsd.init.mk>

.if defined(EMBER_TEXINFO_SOURCE)
PROGS=          info install-info
.NOPATH:        ${PROGS}
.for _P in ${PROGS}
SRCS.${_P}=
.endfor
MAN=            info.1 install-info.1
.PATH:          ${IDIST}/man

GNU_OBJDIR=     ${.OBJDIR}/gnu-build
GNU_BUILD_TRIPLE= ${EMBER_HOST_GNU_TRIPLE}
GNU_HOST_TRIPLE= ${MACHINE_GNU_PLATFORM}
GNU_MAKE_DIRS=  gnulib/lib install-info info
GNU_HOST_MAKE_DIRS= tools/gnulib/lib tools/info
GNU_HOST_MAKE_TARGETS.tools/info= makedoc
GNU_CONFIGURE_ENV= BUILD_CC=${HOST_CC:Q} BUILD_AR=${HOST_AR:Q} \
    BUILD_RANLIB=${HOST_RANLIB:Q} LD_FOR_BUILD=${HOST_LD:Q} \
    CPPFLAGS_FOR_BUILD=${HOST_CPPFLAGS:Q} CFLAGS_FOR_BUILD=${HOST_CFLAGS:Q} \
    LDFLAGS_FOR_BUILD=${HOST_LDFLAGS:Q}
# Keep configure probe warnings visible; enforce OS warnings when compiling.
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
# The common Ports patch expects the configured package and X11 Info paths.
CPPFLAGS+= -DPKGSRC_INFOPATH='"/usr/pkg/info:/usr/X11R6/info:/usr/X11/info"'
.if ${MKNLS} == "no"
GNU_CONFIGURE_ARGS+= --disable-nls
.else
GNU_CONFIGURE_ARGS+= --with-base-libintl
.endif

.include <bsd.gnutarget.mk>
.include <bsd.prog.mk>

_GNU_NAME.info= ginfo
_GNU_NAME.install-info= ginstall-info
.for _P in ${PROGS}
${GNU_OBJDIR}/${_P}/${_GNU_NAME.${_P}}: gnu-target-build
	@test -f ${.TARGET:Q}
${_P}: ${GNU_OBJDIR}/${_P}/${_GNU_NAME.${_P}}
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}
.if defined(_PROGDEBUG.${_P})
${_PROGDEBUG.${_P}}: ${_P} __progdebug
.endif
.endfor
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_TEXINFO_SOURCE to the prepared common Ports Texinfo source
.endif
.include <bsd.prog.mk>
.endif # EMBER_TEXINFO_SOURCE
