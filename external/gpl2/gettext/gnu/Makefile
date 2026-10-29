# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
.include <bsd.init.mk>

.if defined(EMBER_GETTEXT_SOURCE) && defined(EMBER_LIBXML2_SOURCE)
.if (make(obj) || make(includes)) && empty(.MAKEFLAGS:M-V*)
.include <bsd.obj.mk>
.PHONY: includes
includes:
.else
.if empty(.MAKEFLAGS:M-V*) && !defined(HOST_OD) && (make(all) || make(dependall) || make(install) || make(gnu-target-build))
.error Set HOST_OD to the selected native od
.endif
PROGS= msgattrib msgcat msgcmp msgcomm msgconv msgen msgexec msgfilter msgfmt \
    msginit msgmerge msgunfmt msguniq xgettext
.NOPATH: ${PROGS}
.for _P in ${PROGS}
SRCS.${_P}=
MAN+= ${_P}.1
.endfor
.PATH: ${EMBER_GETTEXT_SOURCE}/gettext-tools/man

GNU_SOURCE= ${EMBER_GETTEXT_SOURCE}
GNU_OBJDIR= ${.OBJDIR}/gnu-build
GNU_BUILD_TRIPLE= ${EMBER_HOST_GNU_TRIPLE}
GNU_HOST_TRIPLE= ${MACHINE_GNU_PLATFORM}
GNU_CONFIGURE_ARGS= --prefix=/usr --disable-shared --enable-static \
    --disable-csharp --disable-java --disable-openmp --with-included-libcroco \
    --without-included-libxml --with-xz --without-git --without-emacs \
    --without-included-gettext
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
GNU_CONFIGURE_ENV= EMBER_GETTEXT_PLAIN_STYLE=yes \
    MSGFMT=${HOST_MSGFMT:Q} GMSGFMT=${HOST_MSGFMT:Q} \
    XGETTEXT=${HOST_XGETTEXT:Q} MSGMERGE=${HOST_MSGMERGE:Q}
.if ${MKNLS} == "no"
GNU_CONFIGURE_ARGS+= --disable-nls
.else
GNU_CONFIGURE_ENV+= EMBER_GETTEXT_BASE_LIBINTL=yes
.endif
# The released libtool macros use POSIX od to identify target ELF class.
# Keep this optional native tool local to this GNU consumer.
_GNU_HOST_TOOLS= ${.OBJDIR}/host-tools
GNU_CONFIGURE_ENV+= PATH=${_GNU_HOST_TOOLS:Q}:${EMBER_TARGET_TOOL_DIR:Q}/bin:${EMBER_NATIVE_TOOLS:Q} \
    EMBER_SELECTED_OD=${HOST_OD:Q}
GNU_MAKE_ENV= PATH=${_GNU_HOST_TOOLS:Q}:${EMBER_TARGET_TOOL_DIR:Q}/bin:${EMBER_NATIVE_TOOLS:Q}
GNU_MAKE_DIRS= gettext-runtime/intl gettext-tools/gnulib-lib \
    gettext-tools/libgrep gettext-tools/src
GNU_MAKE_TARGETS.gettext-runtime/intl= libgnuintl.h

# The common XML producer runs first in the parent subdirectory graph.
_XML_QUERY_FLAGS=
.if defined(MAKEOBJDIRPREFIX)
_XML_QUERY_FLAGS+= MAKEOBJDIRPREFIX=${MAKEOBJDIRPREFIX:Q}
.endif
_XML_OBJ!= ${MAKE} -C ${.CURDIR:Q}/../../../bsd/libxml2/gnu ${_XML_QUERY_FLAGS} -V .OBJDIR
_XML_BUILD= ${_XML_OBJ}/gnu-build
.if defined(HOST_PKG_CONFIG) && exists(${_XML_BUILD}/libxml-2.0.pc)
_XML_DEPS!= env PKG_CONFIG_SYSROOT_DIR=${DESTDIR:Q} \
    PKG_CONFIG_LIBDIR=${_XML_BUILD:Q}:${DESTDIR:Q}/usr/lib/pkgconfig:${DESTDIR:Q}/usr/share/pkgconfig \
    ${HOST_PKG_CONFIG:Q} --static --libs libxml-2.0
.endif
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install)) && empty(_XML_DEPS)
.error The common libxml2 producer must provide target pkg-config metadata
.endif
CPPFLAGS+= -I${_XML_BUILD}/include -I${EMBER_LIBXML2_SOURCE}/include
LDFLAGS+= -L${_XML_BUILD}/.libs
GNU_CONFIGURE_ENV+= LIBS=${_XML_DEPS:Q}
GNU_CONFIGURE_DEPENDENCY= ${_XML_BUILD}/.libs/libxml2.a
.include <bsd.gnutarget.mk>
.PHONY: gettext-host-tools
gnu-target-config-inputs: gettext-host-tools
gettext-host-tools:
	mkdir -p ${_GNU_HOST_TOOLS:Q}
	@tmp=${_GNU_HOST_TOOLS:Q}/.od.$$$$; \
	    ln -s ${HOST_OD:Q} "$$tmp" && mv "$$tmp" ${_GNU_HOST_TOOLS:Q}/od
.include <bsd.prog.mk>
.for _P in ${PROGS}
${GNU_OBJDIR}/gettext-tools/src/${_P}: gnu-target-build
	@${HOST_SH:Q} -c 'test -f "$$1"' sh ${.TARGET:Q}
${_P}: ${GNU_OBJDIR}/gettext-tools/src/${_P}
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}
.if defined(_PROGDEBUG.${_P})
${_PROGDEBUG.${_P}}: ${_P} __progdebug
.endif
.endfor
.endif
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install))
.error Set EMBER_GETTEXT_SOURCE and EMBER_LIBXML2_SOURCE to the prepared common Ports sources
.endif
.include <bsd.prog.mk>
.endif
