# Configure the prepared common OpenSSH source for the selected target.
.include <bsd.own.mk>
.include "${NETBSDSRCDIR}/crypto/external/bsd/openssh/source.mk"
OPENSSH_CONFIG_ROLE?= os
OPENSSH_OS_CONFIG_OBJECT?= ${.OBJDIR}/generated
OPENSSH_PRIVATE_LIBSSH_CONFIG_OBJECT?= ${.OBJDIR}/private-libssh
.if ${OPENSSH_CONFIG_ROLE} == "private-libssh"
OPENSSH_CONFIG_OBJECT?= ${OPENSSH_PRIVATE_LIBSSH_CONFIG_OBJECT}
.else
OPENSSH_CONFIG_OBJECT?= ${OPENSSH_OS_CONFIG_OBJECT}
.endif

.if !(!empty(.TARGETS) && empty(.TARGETS:Nobj:Nclean:Ncleandir:Nincludes:Ninstall))
GNU_SOURCE= ${OPENSSH_CONFIG_OBJECT}/source-view
GNU_OBJDIR= ${OPENSSH_CONFIG_OBJECT}/build
GNU_BUILD_TRIPLE= ${EMBER_HOST_GNU_TRIPLE}
GNU_HOST_TRIPLE= ${MACHINE_GNU_PLATFORM}
# Autoconf's generated function probes intentionally redeclare library
# symbols; their warnings must not turn present libc functions into negatives.
# Production compilation retains the OS warning policy, including -Werror.
GNU_CONFIGURE_CFLAGS= ${CFLAGS:N-Werror}
.if ${USE_KERBEROS} != "no"
CPPFLAGS+= ${OPENSSH_KRB5_CPPFLAGS}
.endif
GNU_CONFIGURE_DEPENDENCY= ${OPENSSH_CONFIG_OBJECT}/.configure-source-inputs
GNU_CONFIGURE_ARGS= --prefix=/usr --sysconfdir=/etc/ssh --without-rpath \
    --with-zlib=${DESTDIR}/usr
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
GNU_CONFIGURE_ARGS+= --with-sandbox=rlimit
.endif
.if ${MACHINE_ARCH} == "m68000"
GNU_CONFIGURE_ARGS+= --without-openssl
.else
GNU_CONFIGURE_ARGS+= --with-ssl-dir=${DESTDIR}/usr
.endif
.if ${OPENSSH_CONFIG_ROLE} == "private-libssh"
GNU_CONFIGURE_ARGS+= --without-pam
.elif ${USE_PAM} != "no"
GNU_CONFIGURE_ARGS+= --with-pam
.endif
.if ${USE_KERBEROS} != "no"
GNU_CONFIGURE_ARGS+= --with-kerberos5=${DESTDIR}/usr
.endif

.include <bsd.gnutarget.mk>
all depend dependall: .PHONY
	${HOST_PYTHON:Q} -B ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/tools/configure-source.py \
	    --source ${EMBER_OPENSSH_SOURCE:Q} --os-source ${NETBSDSRCDIR:Q} \
	    --object ${OPENSSH_CONFIG_OBJECT:Q} --autoconf ${HOST_AUTOCONF:Q} \
	    --role ${OPENSSH_CONFIG_ROLE:Q} \
	    --make ${MAKE:Q} --makefile-dir ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/config
.PHONY: openssh-target-config
openssh-target-config: ${GNU_OBJDIR}/Makefile
	@${HOST_SH:Q} -c 'test -f "$$1"' sh ${GNU_OBJDIR:Q}/config.h
.endif

includes install: .PHONY
clean cleandir: .PHONY
	${HOST_PYTHON:Q} -B ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/tools/configure-source.py \
	    --clean --objdir ${.OBJDIR:Q} --object ${OPENSSH_CONFIG_OBJECT:Q} \
	    --os-source ${NETBSDSRCDIR:Q} \
	    --clean-helper ${NETBSDSRCDIR:Q}/share/mk/waf-clean.py
.if ${OPENSSH_CONFIG_OBJECT} != ${OPENSSH_OS_CONFIG_OBJECT}
	${HOST_PYTHON:Q} -B ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/tools/configure-source.py \
	    --clean --objdir ${.OBJDIR:Q} --object ${OPENSSH_OS_CONFIG_OBJECT:Q} \
	    --os-source ${NETBSDSRCDIR:Q} \
	    --clean-helper ${NETBSDSRCDIR:Q}/share/mk/waf-clean.py
.endif
.if ${OPENSSH_CONFIG_OBJECT} != ${OPENSSH_PRIVATE_LIBSSH_CONFIG_OBJECT}
	${HOST_PYTHON:Q} -B ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/tools/configure-source.py \
	    --clean --objdir ${.OBJDIR:Q} --object ${OPENSSH_PRIVATE_LIBSSH_CONFIG_OBJECT:Q} \
	    --os-source ${NETBSDSRCDIR:Q} \
	    --clean-helper ${NETBSDSRCDIR:Q}/share/mk/waf-clean.py
.endif

.include <bsd.obj.mk>
