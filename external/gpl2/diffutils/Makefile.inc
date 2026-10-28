# Origin: EmberBSD (AI-assisted).
# diff and diff3 use the shared Ports source selected by the build.
.if defined(EMBER_DIFFUTILS_SOURCE)
IDIST= ${EMBER_DIFFUTILS_SOURCE}
.endif
