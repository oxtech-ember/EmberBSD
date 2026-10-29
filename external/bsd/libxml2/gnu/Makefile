# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
.include <bsd.init.mk>

.if defined(EMBER_LIBXML2_SOURCE)
.if empty(.MAKEFLAGS:M-V*) && (!defined(HOST_PKG_CONFIG) || !defined(GNU_TARGET_LIBTOOL) || \
    !defined(EMBER_TARGET_TOOL_DIR) || !defined(EMBER_NATIVE_TOOLS)) && \
    (make(all) || make(dependall) || make(gnu-target-build))
.error The common libxml2 producer requires selected host and target tools
.endif
.if !empty(.MAKEFLAGS:M-V*) || (!make(obj) && !make(includes))
GNU_SOURCE= ${EMBER_LIBXML2_SOURCE}
GNU_OBJDIR= ${.OBJDIR}/gnu-build
GNU_BUILD_TRIPLE= ${EMBER_HOST_GNU_TRIPLE}
GNU_HOST_TRIPLE= ${MACHINE_GNU_PLATFORM}
# This static archive is linked into the base PIE executables.
CFLAGS+= -fPIC
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
GNU_CONFIGURE_ENV= PKG_CONFIG=${HOST_PKG_CONFIG:Q} \
    PKG_CONFIG_SYSROOT_DIR=${DESTDIR:Q} \
    PKG_CONFIG_LIBDIR=${DESTDIR:Q}/usr/lib/pkgconfig:${DESTDIR:Q}/usr/share/pkgconfig
GNU_MAKE_ENV= PATH=${EMBER_TARGET_TOOL_DIR:Q}/bin:${EMBER_NATIVE_TOOLS:Q}
GNU_CONFIGURE_ARGS= --prefix=/usr --disable-shared --enable-static \
    --without-icu --with-zlib --with-iconv --with-legacy --with-http --without-python
GNU_MAKE_DIRS= include .
GNU_MAKE_TARGETS.include= all
# The selected shared libtool carries its own shared/static defaults. Preserve
# OS link flags while requesting an archive for this library-only producer.
GNU_MAKE_TARGETS..= LDFLAGS=${LDFLAGS:Q}\ ${LD_DRIVER_FLAGS:Q}\ -static libxml2.la
.include <bsd.gnutarget.mk>
.PHONY: all dependall
all dependall: gnu-target-build
.endif
.else
.if empty(.MAKEFLAGS:M-V*) && (make(all) || make(dependall) || make(gnu-target-build))
.error Set EMBER_LIBXML2_SOURCE to the prepared common Ports libxml2 source
.endif
.endif
.include <bsd.obj.mk>
.PHONY: includes install
includes install:
