# Native libwrap selection; preserve the existing default for both source owners.
WITH_NETBSD_LIBWRAP?=yes
.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE)
SSHDIST?= ${EMBER_OPENSSH_SOURCE}
.if "${SSHDIST}" != "${EMBER_OPENSSH_SOURCE}"
.error SSHDIST must match the prepared common OpenSSH source
.endif
.elif empty(.MAKEFLAGS:M-V*) && \
    !(!empty(.TARGETS) && empty(.TARGETS:Nobj:Nclean:Ncleandir))
.error Set EMBER_OPENSSH_SOURCE to the prepared common Ports source
.else
SSHDIST?= ${NETBSDSRCDIR}/crypto/external/bsd/openssh/dist
.endif

.if defined(EMBER_OPENSSH_SOURCE) && !empty(EMBER_OPENSSH_SOURCE) && \
    ${USE_KERBEROS:Uyes} != "no"
# Shared Heimdal include paths for configure probes and target translation units.
OPENSSH_KRB5_CPPFLAGS= -I${DESTDIR}/usr/include/krb5 \
    -I${DESTDIR}/usr/include/gssapi
.endif
