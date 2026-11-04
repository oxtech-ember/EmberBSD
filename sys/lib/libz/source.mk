# Common Ports zlib source for the base system and standalone consumers.
.if !defined(_EMBER_ZLIB_SOURCE_MK_)
_EMBER_ZLIB_SOURCE_MK_=1
.if !defined(EMBER_ZLIB_SOURCE) || empty(EMBER_ZLIB_SOURCE)
.error Set EMBER_ZLIB_SOURCE to the prepared common Ports source
.elif empty(EMBER_ZLIB_SOURCE:M/*)
.error EMBER_ZLIB_SOURCE must be an absolute path
.endif
.for input in zlib.h zconf.h zutil.h inflate.c inftrees.c emberbsd-prepared-source.json
.if !exists(${EMBER_ZLIB_SOURCE}/${input})
.error Missing prepared zlib input: ${EMBER_ZLIB_SOURCE}/${input}
.endif
.endfor
.endif
