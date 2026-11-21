# Native libwrap selection; preserve the existing default for both source owners.
REPRO_EXTERNAL_SOURCE_VARS+= EMBER_OPENSSH_SOURCE
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

# Each OS configuration must get the same inputs from each consumer, or one
# consumer configures OpenSSH again for the other. Rescue programs build
# without Kerberos, so they use a separate OS configuration. Each consumer
# with Kerberos gives both forms of each library, whatever it links.
.if ${USE_KERBEROS:Uyes} != "no"
OPENSSH_OS_CONFIG_NAME= generated
OPENSSH_KRB5_CONFIG_LIBS=
.for _lib in kafs krb5 com_err asn1 roken des crypto
OPENSSH_KRB5_CONFIG_LIBS+= --target-lib ${DESTDIR:Q}/usr/lib/lib${_lib}.so \
    --target-lib ${DESTDIR:Q}/usr/lib/lib${_lib}.a
.endfor
.else
OPENSSH_OS_CONFIG_NAME= generated-no-kerberos
OPENSSH_KRB5_CONFIG_LIBS=
.endif
