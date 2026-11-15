# Select the OpenSSH crypto implementation for every OS and private-libssh
# consumer, including target configure generation.
# The prepared-source export list is currently OpenSSL-specific; the explicit
# built-in mode is supported only in static, non-PIC builds until its shared
# library ABI list is maintained separately. Unset defaults retain legacy
# platform selection.
.if defined(EMBER_OPENSSH_CRYPTO)
.if ${EMBER_OPENSSH_CRYPTO} != "openssl" && ${EMBER_OPENSSH_CRYPTO} != "builtin"
.error EMBER_OPENSSH_CRYPTO must be openssl or builtin
.endif
OPENSSH_CRYPTO_MODE:= ${EMBER_OPENSSH_CRYPTO}
.elif ${MACHINE_ARCH} == "m68000"
OPENSSH_CRYPTO_MODE= builtin
.else
OPENSSH_CRYPTO_MODE= openssl
.endif

.if defined(EMBER_OPENSSH_CRYPTO) && ${OPENSSH_CRYPTO_MODE} == "builtin" && \
    defined(WITH_OPENSSL)
.error EMBER_OPENSSH_CRYPTO=builtin conflicts with WITH_OPENSSL
.endif

.if defined(EMBER_OPENSSH_CRYPTO) && ${OPENSSH_CRYPTO_MODE} == "builtin" && \
    ${MKPIC} != "no"
.error EMBER_OPENSSH_CRYPTO=builtin currently requires MKPIC=no
.endif
