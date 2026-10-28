# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
.include <bsd.init.mk>

.if defined(EMBER_TDB_SOURCE)
WAF_SOURCE= ${EMBER_TDB_SOURCE}
WAF_OBJDIR= ${.OBJDIR}/waf-build
WAF_TARGETS= tdb
WAF_CONFIGURE_INPUTS= wscript buildtools third_party/waf lib/replace
WAF_CONFIGURE_ARGS= --prefix=/usr --libdir=${LIBDIR} --disable-python \
	--disable-rpath --with-libiconv= --disable-symbol-versions \
	--abi-check-disable --cross-compile
.if ${MKSTATICLIB} != "no"
WAF_CONFIGURE_ARGS+= --enable-static-archive
.endif
.if defined(EMBER_TDB_CROSS_ANSWERS)
WAF_ANSWERS_SOURCE= ${EMBER_TDB_CROSS_ANSWERS}
.elif ${MACHINE} == "evbarm" && ${MACHINE_ARCH} == "aarch64"
WAF_ANSWERS_SOURCE= ${.CURDIR}/../answers/evbarm-aarch64
.endif
.if defined(WAF_ANSWERS_SOURCE)
WAF_CONFIGURE_ARGS+= --cross-answers=${WAF_OBJDIR}/answers
.endif
.if defined(EMBER_TDB_CROSS_EXECUTE)
WAF_CONFIGURE_ARGS+= --cross-execute=${EMBER_TDB_CROSS_EXECUTE:Q}
.endif

.PATH: ${EMBER_TDB_SOURCE}/include
INCS= tdb.h
INCSDIR= /usr/include
FILESDIR= ${LIBDIR}
FILESBUILD= yes
FILESOWN= ${LIBOWN}
FILESGRP= ${LIBGRP}
FILESMODE= ${LIBMODE}
FILES=
.if ${MKSTATICLIB} != "no"
realall: libtdb.a
.if ${MKLINKLIB} != "no"
FILES+= libtdb.a
.endif
.endif
.if ${MKPIC} != "no"
FILES+= libtdb.so.1.4.15
SYMLINKS= libtdb.so.1.4.15 ${LIBDIR}/libtdb.so.1
.if ${MKLINKLIB} != "no"
SYMLINKS+= libtdb.so.1.4.15 ${LIBDIR}/libtdb.so
.endif
.if ${MKDEBUG:Uno} != "no"
FILES+= libtdb.so.1.4.15.debug
FILESDIR_libtdb.so.1.4.15.debug= ${DEBUGDIR}${LIBDIR}
FILESMODE_libtdb.so.1.4.15.debug= ${DEBUGMODE}
.endif
.endif

.include <bsd.waftarget.mk>
.include <bsd.prog.mk>

libtdb.a: waf-target-build
	cmp -s ${WAF_OBJDIR:Q}/default/libtdb.a ${.TARGET:Q} || \
	    { cp -p ${WAF_OBJDIR:Q}/default/libtdb.a ${.TARGET:Q}.tmp && \
	      mv ${.TARGET:Q}.tmp ${.TARGET:Q}; }
libtdb.so.1.4.15.link: waf-target-build
	@if ! cmp -s ${WAF_OBJDIR:Q}/default/libtdb.so ${.TARGET:Q}; then \
	    for f in libtdb.so.1.4.15 libtdb.so.1.4.15.debug; do \
	        if test -f "$$f"; then rm "$$f" || exit 1; fi; \
	    done; \
	    cp -p ${WAF_OBJDIR:Q}/default/libtdb.so ${.TARGET:Q}.tmp && \
	        mv ${.TARGET:Q}.tmp ${.TARGET:Q} || exit 1; \
	fi
libtdb.so.1.4.15: libtdb.so.1.4.15.link
	cp -p ${.ALLSRC:[1]:Q} ${.TARGET:Q}.tmp && \
	    mv ${.TARGET:Q}.tmp ${.TARGET:Q}
.if ${MKPIC} != "no" && ${MKDEBUG:Uno} != "no"
libtdb.so.1.4.15.debug: libtdb.so.1.4.15 __progdebug
.ORDER: libtdb.so.1.4.15.debug ${DESTDIR}${FILESDIR}/libtdb.so.1.4.15
.endif
CLEANFILES+= libtdb.so.1.4.15.link
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(install) || make(realall) || \
    make(realinstall) || make(filesbuild) || make(waf-target-build))
.error Set EMBER_TDB_SOURCE to the prepared common Ports tdb source
.endif
.include <bsd.prog.mk>
.endif
