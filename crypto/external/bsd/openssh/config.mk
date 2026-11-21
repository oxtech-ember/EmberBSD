.if !defined(_EMBER_OPENSSH_CONFIG_MK_)
_EMBER_OPENSSH_CONFIG_MK_= 1
# Shared target-generated OpenSSH config for native OS consumers.
.include "${NETBSDSRCDIR}/crypto/external/bsd/openssh/crypto.mk"
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
.if empty(.MAKEFLAGS:M-V*) && \
    !(!empty(.TARGETS) && empty(.TARGETS:Nobj:Nclean:Ncleandir:Nincludes:Ninstall)) && \
    (!defined(HOST_AUTOCONF) || empty(HOST_AUTOCONF))
.error OpenSSH target config requires selected HOST_AUTOCONF
.endif
OPENSSH_CONFIG_ROLE?= os
OPENSSH_OS_CONFIG_OBJECT?= ${_SRC_TOP_OBJ_}/crypto/external/bsd/openssh/config/${OPENSSH_OS_CONFIG_NAME}
OPENSSH_PRIVATE_LIBSSH_CONFIG_OBJECT?= ${_SRC_TOP_OBJ_}/crypto/external/bsd/openssh/config/private-libssh
.if ${OPENSSH_CONFIG_ROLE} == "private-libssh"
OPENSSH_CONFIG_OBJECT?= ${OPENSSH_PRIVATE_LIBSSH_CONFIG_OBJECT}
.else
OPENSSH_CONFIG_OBJECT?= ${OPENSSH_OS_CONFIG_OBJECT}
.endif
OPENSSH_CONFIG_HEADER= ${OPENSSH_CONFIG_OBJECT}/build/config.h
CPPFLAGS+= -I${OPENSSH_CONFIG_OBJECT}/build
CPPFLAGS+= -I${OPENSSH_CONFIG_OBJECT}/build/openbsd-compat/include
DPSRCS+= ${OPENSSH_CONFIG_HEADER}

# Every consumer checks the shared configuration; the helper serializes
# generation and the GNU adapter preserves a warm configuration unchanged.
openssh-config-check: .PHONY
${OPENSSH_CONFIG_HEADER}: openssh-config-check
	${HOST_PYTHON:Q} -B ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/tools/configure-source.py \
	    --source ${EMBER_OPENSSH_SOURCE:Q} --os-source ${NETBSDSRCDIR:Q} \
	    --object ${OPENSSH_CONFIG_OBJECT:Q} --autoconf ${HOST_AUTOCONF:Q} \
	    --role ${OPENSSH_CONFIG_ROLE:Q} \
	    --crypto-mode ${OPENSSH_CRYPTO_MODE:Q} \
	    ${OPENSSH_KRB5_CONFIG_LIBS} \
	    --make ${MAKE:Q} --makefile-dir ${NETBSDSRCDIR:Q}/crypto/external/bsd/openssh/config
.endif


.endif
