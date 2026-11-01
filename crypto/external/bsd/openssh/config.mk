.if !defined(_EMBER_OPENSSH_CONFIG_MK_)
_EMBER_OPENSSH_CONFIG_MK_= 1
# Shared target-generated OpenSSH config for native OS consumers.
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
.if empty(.MAKEFLAGS:M-V*) && \
    !(!empty(.TARGETS) && empty(.TARGETS:Nobj:Nclean:Ncleandir:Nincludes:Ninstall)) && \
    (!defined(HOST_AUTOCONF) || empty(HOST_AUTOCONF))
.error OpenSSH target config requires selected HOST_AUTOCONF
.endif
OPENSSH_CONFIG_OBJECT?= ${_SRC_TOP_OBJ_}/crypto/external/bsd/openssh/config/generated
OPENSSH_CONFIG_HEADER= ${OPENSSH_CONFIG_OBJECT}/build/config.h
CPPFLAGS+= -I${OPENSSH_CONFIG_OBJECT}/build
CPPFLAGS+= -I${OPENSSH_CONFIG_OBJECT}/build/openbsd-compat/include
DPSRCS+= ${OPENSSH_CONFIG_HEADER}

# Every consumer checks the shared configuration; the helper serializes
# generation and the GNU adapter preserves a warm configuration unchanged.
_OPENSSH_KRB5_CONFIG_LIBS=
.if ${USE_KERBEROS:Uyes} != "no"
.for _lib in kafs krb5 com_err asn1 roken des crypto
_OPENSSH_KRB5_CONFIG_LIBS+= --target-lib ${DESTDIR:Q}/usr/lib/lib${_lib}.so
.if !empty(LDSTATIC:M-static) || !exists(${DESTDIR}/usr/lib/lib${_lib}.so)
_OPENSSH_KRB5_CONFIG_LIBS+= --target-lib ${DESTDIR:Q}/usr/lib/lib${_lib}.a
.endif
.endfor
.endif

openssh-config-check: .PHONY
${OPENSSH_CONFIG_HEADER}: openssh-config-check
	${HOST_PYTHON:Q} -B ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/tools/configure-source.py \
	    --source ${EMBER_OPENSSH_SOURCE:Q} --os-source ${NETBSDSRCDIR:Q} \
	    --object ${OPENSSH_CONFIG_OBJECT:Q} --autoconf ${HOST_AUTOCONF:Q} \
	    ${_OPENSSH_KRB5_CONFIG_LIBS} \
	    --make ${MAKE:Q} --makefile-dir ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/config
.endif


.endif
