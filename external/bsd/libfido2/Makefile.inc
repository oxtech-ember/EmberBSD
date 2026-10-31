# $NetBSD: Makefile.inc,v 1.4 2021/06/18 13:57:52 christos Exp $

.if !defined(EMBER_LIBFIDO2_SOURCE) && !(make(obj) && empty(.TARGETS:Nobj))
.error Set EMBER_LIBFIDO2_SOURCE to the prepared common Ports source
.endif

.if defined(EMBER_LIBFIDO2_SOURCE)
DIST:= ${EMBER_LIBFIDO2_SOURCE}
.else
DIST:=${.PARSEDIR}/dist
.endif

VERS_FILE=${DIST}/CMakeLists.txt

.if defined(EMBER_LIBFIDO2_SOURCE)
.for i in MAJOR MINOR PATCH
FIDO_${i} != \
    ${TOOL_SED} -ne '/FIDO_${i}/s/.*"\([0-9]*\)")$$/\1/p' ${VERS_FILE}
.endfor
FIDO_VERSION=${FIDO_MAJOR}.${FIDO_MINOR}.${FIDO_PATCH}
.else
# Object-directory setup can run before the prepared source is selected.
FIDO_MAJOR= 0
FIDO_MINOR= 0
FIDO_PATCH= 0
FIDO_VERSION= 0.0.0
.endif

CPPFLAGS+=-D_FIDO_MAJOR=${FIDO_MAJOR} -D_FIDO_MINOR=${FIDO_MINOR}
CPPFLAGS+=-D_FIDO_PATCH=${FIDO_PATCH}
CPPFLAGS+=-DHAVE_UNISTD_H -DHAVE_ARC4RANDOM_BUF -DHAVE_TIMESPECSUB
