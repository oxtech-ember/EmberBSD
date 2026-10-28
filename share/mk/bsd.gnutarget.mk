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
# Optional native generators run in these directories before target builds.
# Set GNU_HOST_MAKE_TARGETS.<directory> to override the default all target.
GNU_HOST_MAKE_DIRS?=
GNU_HOST_MAKE_ENV?=
# Each invocation consumes one bmake job slot.
GNU_CONFIGURE_ARGS?=
GNU_CONFIGURE_ENV?=
GNU_MAKE_ENV?=
GNU_CONFIGURE_CFLAGS?= ${CFLAGS}
_GNU_TARGET_ENV= CC=${CC:Q} CXX=${CXX:Q} CPP=${CPP:Q} AR=${AR:Q} \
	AS=${AS:Q} LD=${LD:Q} NM=${NM:Q} RANLIB=${RANLIB:Q} STRIP=${STRIP:Q} \
	CPPFLAGS=${CPPFLAGS:Q} \
	CFLAGS=${GNU_CONFIGURE_CFLAGS:Q} CXXFLAGS=${CXXFLAGS:Q} \
	LDFLAGS=${LDFLAGS:Q}\ ${LD_DRIVER_FLAGS:Q}
.if !empty(GNU_HOST_MAKE_DIRS)
_GNU_HOST_CONTEXT= ${GNU_HOST_MAKE_DIRS:Q} ${GNU_HOST_MAKE_ENV:Q} \
	${HOST_CC:Q} ${HOST_CXX:Q} ${HOST_AR:Q} ${HOST_RANLIB:Q} ${HOST_LD:Q} \
	${HOST_CPPFLAGS:Q} ${HOST_CFLAGS:Q} ${HOST_CXXFLAGS:Q} ${HOST_LDFLAGS:Q}
.for _GNU_DIR in ${GNU_HOST_MAKE_DIRS}
_GNU_HOST_CONTEXT+= ${GNU_HOST_MAKE_TARGETS.${_GNU_DIR}:Uall:Q}
.endfor
.endif

# Do not let bmake or nested GNU make select the host's default shell.
.SHELL: name=sh path=${HOST_SH}

.PHONY: gnu-target-config-inputs
gnu-target-config-inputs:
	mkdir -p ${GNU_OBJDIR:Q}
	@tmp=${GNU_OBJDIR:Q}/.configure-inputs.tmp.$$$$; \
	    cp ${GNU_SOURCE:Q}/configure "$$tmp.source" || exit 1; \
	    printf '%s\n' ${GNU_SOURCE:Q} ${GNU_BUILD_TRIPLE:Q} ${GNU_HOST_TRIPLE:Q} \
	    ${HOST_SH:Q} ${HOST_GMAKE:Q} ${_GNU_TARGET_ENV:Q} \
	    ${CFLAGS:Q} ${CXXFLAGS:Q} ${GNU_MAKE_ENV:Q} \
	    ${_GNU_HOST_CONTEXT} \
	    ${GNU_CONFIGURE_ARGS:Q} ${GNU_CONFIGURE_ENV:Q} > "$$tmp" || exit 1; \
	    if test ! -f ${GNU_OBJDIR:Q}/.cleanup-makefile && \
	       test -f ${GNU_OBJDIR:Q}/.configure-inputs && \
	       cmp -s "$$tmp" ${GNU_OBJDIR:Q}/.configure-inputs && \
	       cmp -s "$$tmp.source" ${GNU_OBJDIR:Q}/.configure-source; then \
	        rm "$$tmp" "$$tmp.source"; \
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
	            (cd ${GNU_OBJDIR:Q} && env MAKEFLAGS= MAKEOVERRIDES= SHELL=${HOST_SH:Q} \
	                ${HOST_GMAKE:Q} -j1 SHELL=${HOST_SH:Q} distclean) || exit 1; \
	            test ! -f ${GNU_OBJDIR:Q}/Makefile || rm ${GNU_OBJDIR:Q}/Makefile || exit 1; \
	        fi; \
	        mv "$$tmp.source" ${GNU_OBJDIR:Q}/.configure-source || exit 1; \
	        mv "$$tmp" ${GNU_OBJDIR:Q}/.configure-inputs || exit 1; \
	        if test -f ${GNU_OBJDIR:Q}/.cleanup-makefile; then \
	            rm ${GNU_OBJDIR:Q}/.cleanup-makefile || exit 1; \
	        fi; \
	    fi

${GNU_OBJDIR}/.configure-inputs: gnu-target-config-inputs
	@test -f ${.TARGET:Q}

${GNU_OBJDIR}/Makefile: ${GNU_SOURCE}/configure ${GNU_OBJDIR}/.configure-inputs
	(cd ${GNU_OBJDIR:Q} && \
	    env MAKEFLAGS= MAKEOVERRIDES= CONFIG_SHELL=${HOST_SH:Q} SHELL=${HOST_SH:Q} \
	    ${_GNU_TARGET_ENV} ${GNU_CONFIGURE_ENV} \
	    ${HOST_SH:Q} ${GNU_SOURCE:Q}/configure \
	    --build=${GNU_BUILD_TRIPLE:Q} --host=${GNU_HOST_TRIPLE:Q} \
	    ${GNU_CONFIGURE_ARGS}) || \
	    { test ! -f ${GNU_OBJDIR:Q}/Makefile || rm ${GNU_OBJDIR:Q}/Makefile; false; }

.PHONY: gnu-target-build
gnu-target-build: ${GNU_OBJDIR}/Makefile
.for _GNU_DIR in ${GNU_HOST_MAKE_DIRS}
	cd ${GNU_OBJDIR:Q}/${_GNU_DIR:Q} && env MAKEFLAGS= MAKEOVERRIDES= \
	    SHELL=${HOST_SH:Q} ${GNU_HOST_MAKE_ENV} ${HOST_GMAKE:Q} -j1 \
	    SHELL=${HOST_SH:Q} CC=${HOST_CC:Q} CXX=${HOST_CXX:Q} \
	    AR=${HOST_AR:Q} RANLIB=${HOST_RANLIB:Q} LD=${HOST_LD:Q} \
	    CPPFLAGS=${HOST_CPPFLAGS:Q} CFLAGS=${HOST_CFLAGS:Q} \
	    CXXFLAGS=${HOST_CXXFLAGS:Q} LDFLAGS=${HOST_LDFLAGS:Q} \
	    ${GNU_HOST_MAKE_TARGETS.${_GNU_DIR}:Uall}
.endfor
.for _GNU_DIR in ${GNU_MAKE_DIRS}
	cd ${GNU_OBJDIR:Q}/${_GNU_DIR:Q} && env MAKEFLAGS= MAKEOVERRIDES= SHELL=${HOST_SH:Q} \
	    ${_GNU_TARGET_ENV} ${GNU_MAKE_ENV} ${HOST_GMAKE:Q} \
	    -j1 SHELL=${HOST_SH:Q} CFLAGS=${CFLAGS:Q} CXXFLAGS=${CXXFLAGS:Q} ${GNU_MAKE_TARGETS}
.endfor
