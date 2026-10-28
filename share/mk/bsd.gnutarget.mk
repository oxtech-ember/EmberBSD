# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
# Build a prepared GNU source tree for the target in a caller-owned objdir.
# The consumer installs programs, scripts, manuals and links using bsd rules.
# This adapter currently supports C target producers; C++ link flags are not routed.

.if !defined(GNU_SOURCE) || !defined(GNU_OBJDIR) || \
    !defined(GNU_BUILD_TRIPLE) || !defined(GNU_HOST_TRIPLE) || \
    !defined(HOST_SH) || !defined(HOST_GMAKE)
.error GNU target build requires source, objdir, build/host triples and selected host shell/gmake
.endif

GNU_MAKE_TARGETS?= all
GNU_MAKE_DIRS?= .
# Each invocation consumes one bmake job slot.
GNU_CONFIGURE_ARGS?=
GNU_CONFIGURE_ENV?=
GNU_MAKE_ENV?=
GNU_CONFIGURE_INPUTS?=
GNU_CONFIGURE_CFLAGS?= ${CFLAGS}
_GNU_TARGET_ENV= CC=${CC:Q} CXX=${CXX:Q} CPP=${CPP:Q} AR=${AR:Q} \
	AS=${AS:Q} LD=${LD:Q} NM=${NM:Q} RANLIB=${RANLIB:Q} STRIP=${STRIP:Q} \
	CPPFLAGS=${CPPFLAGS:Q} \
	CFLAGS=${GNU_CONFIGURE_CFLAGS:Q} CXXFLAGS=${CXXFLAGS:Q} \
	LDFLAGS=${LDFLAGS:Q}\ ${LD_DRIVER_FLAGS:Q}

# Do not let bmake or nested GNU make select the host's default shell.
.SHELL: name=sh path=${HOST_SH}

.PHONY: gnu-target-config-inputs
gnu-target-config-inputs:
	mkdir -p ${GNU_OBJDIR:Q}
	@tmp=${GNU_OBJDIR:Q}/.configure-inputs.tmp.$$$$; \
	    printf '%s\n' ${GNU_SOURCE:Q} ${GNU_BUILD_TRIPLE:Q} ${GNU_HOST_TRIPLE:Q} \
	    ${HOST_SH:Q} ${HOST_GMAKE:Q} ${_GNU_TARGET_ENV:Q} \
	    ${CFLAGS:Q} ${CXXFLAGS:Q} ${GNU_MAKE_ENV:Q} \
	    ${GNU_CONFIGURE_ARGS:Q} ${GNU_CONFIGURE_ENV:Q} > "$$tmp" || exit 1; \
	    if test ! -f ${GNU_OBJDIR:Q}/.cleanup-makefile && \
	       test -f ${GNU_OBJDIR:Q}/.configure-inputs && \
	       cmp -s "$$tmp" ${GNU_OBJDIR:Q}/.configure-inputs; then \
	        rm "$$tmp"; \
	    else \
	        if test -f ${GNU_OBJDIR:Q}/.cleanup-makefile; then \
	            cp ${GNU_OBJDIR:Q}/.cleanup-makefile ${GNU_OBJDIR:Q}/.restore-makefile.tmp.$$$$ && \
	                mv ${GNU_OBJDIR:Q}/.restore-makefile.tmp.$$$$ ${GNU_OBJDIR:Q}/Makefile || exit 1; \
	        fi; \
	        if test -f ${GNU_OBJDIR:Q}/Makefile; then \
	            if test ! -f ${GNU_OBJDIR:Q}/.cleanup-makefile; then \
	                cp ${GNU_OBJDIR:Q}/Makefile ${GNU_OBJDIR:Q}/.cleanup-makefile.tmp.$$$$ && \
	                    mv ${GNU_OBJDIR:Q}/.cleanup-makefile.tmp.$$$$ ${GNU_OBJDIR:Q}/.cleanup-makefile || exit 1; \
	            fi; \
	            (cd ${GNU_OBJDIR:Q} && env MAKEFLAGS= SHELL=${HOST_SH:Q} \
	                ${HOST_GMAKE:Q} -j1 SHELL=${HOST_SH:Q} distclean) || exit 1; \
	            test ! -f ${GNU_OBJDIR:Q}/Makefile || rm ${GNU_OBJDIR:Q}/Makefile || exit 1; \
	        fi; \
	        mv "$$tmp" ${GNU_OBJDIR:Q}/.configure-inputs || exit 1; \
	        if test -f ${GNU_OBJDIR:Q}/.cleanup-makefile; then \
	            rm ${GNU_OBJDIR:Q}/.cleanup-makefile || exit 1; \
	        fi; \
	    fi

${GNU_OBJDIR}/.configure-inputs: gnu-target-config-inputs
	@test -f ${.TARGET:Q}

${GNU_OBJDIR}/Makefile: ${GNU_SOURCE}/configure ${GNU_CONFIGURE_INPUTS} ${GNU_OBJDIR}/.configure-inputs
	(cd ${GNU_OBJDIR:Q} && \
	    env CONFIG_SHELL=${HOST_SH:Q} SHELL=${HOST_SH:Q} \
	    ${_GNU_TARGET_ENV} ${GNU_CONFIGURE_ENV} \
	    ${HOST_SH:Q} ${GNU_SOURCE:Q}/configure \
	    --build=${GNU_BUILD_TRIPLE:Q} --host=${GNU_HOST_TRIPLE:Q} \
	    ${GNU_CONFIGURE_ARGS}) || \
	    { test ! -f ${GNU_OBJDIR:Q}/Makefile || rm ${GNU_OBJDIR:Q}/Makefile; false; }

.PHONY: gnu-target-build
gnu-target-build: ${GNU_OBJDIR}/Makefile
.for _GNU_DIR in ${GNU_MAKE_DIRS}
	cd ${GNU_OBJDIR:Q}/${_GNU_DIR:Q} && env MAKEFLAGS= SHELL=${HOST_SH:Q} \
	    ${_GNU_TARGET_ENV} ${GNU_MAKE_ENV} ${HOST_GMAKE:Q} \
	    -j1 SHELL=${HOST_SH:Q} CFLAGS=${CFLAGS:Q} CXXFLAGS=${CXXFLAGS:Q} ${GNU_MAKE_TARGETS}
.endfor
