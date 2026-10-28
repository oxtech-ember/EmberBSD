# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Origin: EmberBSD (AI-assisted).
# Build a selected Waf source for the target; the consumer owns installation.

.if !defined(WAF_SOURCE) || !defined(WAF_OBJDIR)
.error Waf target build requires source and objdir
.endif
.if (!defined(HOST_PYTHON) || !defined(HOST_SH)) && \
    (make(all) || make(dependall) || make(install) || make(realall) || \
     make(realinstall) || make(filesbuild) || \
     make(waf-target-configure) || make(waf-target-build))
.error Waf target build requires selected host Python and shell
.endif

WAF_CONFIGURE_ARGS?=
WAF_CONFIGURE_INPUTS?= wscript
WAF_CONFIGURE_ENV?=
WAF_TARGETS?= all
WAF_CONFIGURE_CFLAGS?= ${CFLAGS:N-Werror}
.if defined(WAF_ANSWERS_SOURCE)
_WAF_ANSWERS_INPUT= ${WAF_ANSWERS_SOURCE:Q}
.endif
_WAF_TARGET_ENV= CC=${CC:Q} AR=${AR:Q} RANLIB=${RANLIB:Q} \
	CPPFLAGS=${CPPFLAGS:Q} CFLAGS=${WAF_CONFIGURE_CFLAGS:Q} \
	ADDITIONAL_CFLAGS=${CFLAGS:Q} LDFLAGS=${LDFLAGS:Q}\ ${LD_DRIVER_FLAGS:Q} \
	PKG_CONFIG_SYSROOT_DIR=${DESTDIR:Q} \
	PKG_CONFIG_LIBDIR=${DESTDIR:Q}/usr/lib/pkgconfig:${DESTDIR:Q}/usr/share/pkgconfig
_WAF_HOST_ENV= WAFLOCK=${WAF_OBJDIR:Q}/.lock-wscript PYTHONHASHSEED=1 \
	PYTHONPATH= PYTHONHOME= SHELL=${HOST_SH:Q}

.if defined(HOST_SH)
.SHELL: name=sh path=${HOST_SH}
.endif
.PHONY: waf-target-configure waf-target-build
waf-target-configure:
	@mkdir -p ${WAF_OBJDIR:Q}
	@tmp=${WAF_OBJDIR:Q}/.inputs.tmp.$$$$; \
	    printf '%s\n' ${WAF_SOURCE:Q} ${HOST_PYTHON:Q} ${HOST_SH:Q} \
	    ${_WAF_TARGET_ENV:Q} ${WAF_CONFIGURE_ENV:Q} ${WAF_CONFIGURE_ARGS:Q} ${WAF_CONFIGURE_INPUTS:Q} ${_WAF_ANSWERS_INPUT} \
	    > "$$tmp" || exit 1; \
	    env PYTHONPATH= PYTHONHOME= PYTHONHASHSEED=1 ${HOST_PYTHON:Q} -B -S \
	        ${NETBSDSRCDIR:Q}/share/mk/waf-config-snapshot.py --source ${WAF_SOURCE:Q} \
	        ${WAF_CONFIGURE_INPUTS:@_input@${_input:Q}@} > "$$tmp.source" || exit 1; \
	    answers_changed=no; \
	    if test x${WAF_ANSWERS_SOURCE:U:Q} != x; then \
	        if ! cmp -s ${WAF_ANSWERS_SOURCE:U:Q} ${WAF_OBJDIR:Q}/.answers-source; then answers_changed=yes; fi; \
	    fi; \
	    if test "$$answers_changed" = no && test -f ${WAF_OBJDIR:Q}/.inputs && \
	       cmp -s "$$tmp" ${WAF_OBJDIR:Q}/.inputs && \
	       cmp -s "$$tmp.source" ${WAF_OBJDIR:Q}/.source-inputs; then \
	        rm "$$tmp" "$$tmp.source"; \
	    else \
	        if test -f ${WAF_OBJDIR:Q}/.configured; then rm ${WAF_OBJDIR:Q}/.configured || exit 1; fi; \
	        mv "$$tmp.source" ${WAF_OBJDIR:Q}/.source-inputs && \
	            mv "$$tmp" ${WAF_OBJDIR:Q}/.inputs || exit 1; \
	    fi; \
	    if test ! -f ${WAF_OBJDIR:Q}/.configured; then \
	        if test x${WAF_ANSWERS_SOURCE:U:Q} != x; then \
	            cp ${WAF_ANSWERS_SOURCE:U:Q} ${WAF_OBJDIR:Q}/answers && \
	                cp ${WAF_ANSWERS_SOURCE:U:Q} ${WAF_OBJDIR:Q}/.answers-source || exit 1; \
	        fi; \
	        (cd ${WAF_SOURCE:Q} && env ${_WAF_HOST_ENV} ${_WAF_TARGET_ENV} ${WAF_CONFIGURE_ENV} \
	            ${HOST_PYTHON:Q} -B -S ${WAF_SOURCE:Q}/buildtools/bin/waf configure \
	            --top=${WAF_SOURCE:Q} --out=${WAF_OBJDIR:Q} ${WAF_CONFIGURE_ARGS}) || exit 1; \
	        : > ${WAF_OBJDIR:Q}/.configured.tmp.$$$$ && \
	            mv ${WAF_OBJDIR:Q}/.configured.tmp.$$$$ ${WAF_OBJDIR:Q}/.configured || exit 1; \
	    fi

waf-target-build: waf-target-configure
	(cd ${WAF_SOURCE:Q} && env ${_WAF_HOST_ENV} ${_WAF_TARGET_ENV} ${WAF_CONFIGURE_ENV} \
	    ${HOST_PYTHON:Q} -B -S ${WAF_SOURCE:Q}/buildtools/bin/waf build \
	    --top=${WAF_SOURCE:Q} --out=${WAF_OBJDIR:Q} --targets=${WAF_TARGETS:Q} -j1)
