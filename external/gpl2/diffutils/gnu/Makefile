# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
.include <bsd.init.mk>

.if defined(EMBER_DIFFUTILS_SOURCE)
PROGS= diff diff3
BINDIR= /usr/bin
.for _P in ${PROGS}
SRCS.${_P}=
.endfor
MAN= diff.1 diff3.1
TEXINFO= diffutils.texi
# Keep the historic Info path while the current manual's directory entries
# refer to its upstream filename.
.if ${MKINFO} != "no"
LINKS+= ${INFODIR}/diffutils.info ${INFODIR}/diff.info
LINKSMODE= ${INFOMODE}
.endif
.PATH: ${EMBER_DIFFUTILS_SOURCE}/man ${EMBER_DIFFUTILS_SOURCE}/doc

GNU_SOURCE= ${EMBER_DIFFUTILS_SOURCE}
GNU_OBJDIR= ${.OBJDIR}/gnu-build
GNU_BUILD_TRIPLE= ${EMBER_HOST_GNU_TRIPLE}
GNU_HOST_TRIPLE= ${MACHINE_GNU_PLATFORM}
GNU_CONFIGURE_ARGS= --prefix=/usr
.if ${MKNLS} == "no"
GNU_CONFIGURE_ARGS+= --disable-nls
.endif
# The common recipe's gdiff setting makes diff3 use the installed diff.
GNU_MAKE_DIRS= lib src
GNU_MAKE_TARGETS= gdiff=/usr/bin/diff all
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}

.include <bsd.gnutarget.mk>
.include <bsd.info.mk>
.include <bsd.prog.mk>

.for _P in ${PROGS}
${GNU_OBJDIR}/src/${_P}: gnu-target-build
	@test -f ${.TARGET:Q}
${_P}: ${GNU_OBJDIR}/src/${_P}
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}
.if defined(_PROGDEBUG.${_P})
${_PROGDEBUG.${_P}}: ${_P} __progdebug
.endif
.endfor
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_DIFFUTILS_SOURCE to the prepared common Ports diffutils source
.endif
.include <bsd.prog.mk>
.endif
