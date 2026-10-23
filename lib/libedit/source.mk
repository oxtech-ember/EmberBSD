# Consume only the source tree prepared from the canonical Ports recipe.
.if !defined(_EMBER_LIBEDIT_SOURCE_MK_)
_EMBER_LIBEDIT_SOURCE_MK_=1

.if !defined(EMBER_LIBEDIT_SOURCE) || empty(EMBER_LIBEDIT_SOURCE)
.error Set EMBER_LIBEDIT_SOURCE to the source tree prepared by EmberBSD-SDK
.elif empty(EMBER_LIBEDIT_SOURCE:M/*)
.error EMBER_LIBEDIT_SOURCE must be an absolute path
.endif

.for input in src/histedit.h src/editline/readline.h src/makelist src/el.c doc/editline.3.roff ports.distinfo libedit-source.json
.if !exists(${EMBER_LIBEDIT_SOURCE}/${input})
.error Missing prepared libedit input: ${EMBER_LIBEDIT_SOURCE}/${input}
.endif
.endfor

.if defined(LIBEDITDIR) && "${LIBEDITDIR}" != "${EMBER_LIBEDIT_SOURCE}/src"
.error LIBEDITDIR must refer to EMBER_LIBEDIT_SOURCE/src
.endif
LIBEDITDIR=${EMBER_LIBEDIT_SOURCE}/src
.endif
