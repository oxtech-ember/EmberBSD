# $NetBSD: Makefile,v 1.5 2024/06/24 18:01:03 riastradh Exp $

NOLINT=
NOMAN=
.include <bsd.own.mk>

.if !defined(EMBER_LIBCBOR_SOURCE) && !(make(obj) && empty(.TARGETS:Nobj))
.error Set EMBER_LIBCBOR_SOURCE to the prepared common Ports source
.endif

.if defined(EMBER_LIBCBOR_SOURCE)
DIST = ${EMBER_LIBCBOR_SOURCE}
.else
DIST = ${.CURDIR}/../dist
.endif

.PATH: ${DIST}/src ${DIST}/src/cbor ${DIST}/src/cbor/internal

CPPFLAGS+= -I${DIST}/src -DHAVE_ENDIAN_H -I. -DEIGHT_BYTE_SIZE_T
.if defined(EMBER_LIBCBOR_SOURCE)
CPPFLAGS+= -include ${.CURDIR:Q}/byte-order.h
.endif

.if ${MACHINE} == "vax"
# vax does not have NaN (no ieee754, so this code will not work anyway)
CPPFLAGS+=-DNAN=INFINITY
.endif

LIB=    cbor

SRCS+=   cbor.c
.if defined(EMBER_LIBCBOR_SOURCE)
SRCS+=   allocators.c
LIB_EXPSYM= ${.CURDIR}/cbor-0.14.expsym
.endif

# For ldexp
LIBDPLIBS = m ${NETBSDSRCDIR}/lib/libm

VERS_FILE=${DIST}/CMakeLists.txt

.if defined(EMBER_LIBCBOR_SOURCE)
CBOR_VERSION!= ${TOOL_SED} -n 's/^project(libcbor VERSION \([0-9.]*\) .*/\1/p' ${VERS_FILE}
CBOR_VERSION_MAJOR= ${CBOR_VERSION:S/./ /g:[1]}
CBOR_VERSION_MINOR= ${CBOR_VERSION:S/./ /g:[2]}
CBOR_VERSION_PATCH= ${CBOR_VERSION:S/./ /g:[3]}
.else
# Object-directory setup can run before the prepared source is selected.
CBOR_VERSION_MAJOR= 0
CBOR_VERSION_MINOR= 0
CBOR_VERSION_PATCH= 0
CBOR_VERSION= 0.0.0
.endif

# cbor/
SRCS+= \
arrays.c \
bytestrings.c \
callbacks.c \
common.c \
encoding.c \
floats_ctrls.c \
ints.c \
maps.c \
serialization.c \
streaming.c \
strings.c \
tags.c

# cbor/internal
SRCS+= \
builder_callbacks.c \
encoders.c \
loaders.c \
memory_utils.c \
stack.c \
unicode.c

INCS+= \
cbor.h \
cbor/arrays.h \
cbor/bytestrings.h \
cbor/callbacks.h \
cbor/common.h \
cbor/configuration.h \
${defined(EMBER_LIBCBOR_SOURCE):?cbor/cbor_export.h:} \
cbor/data.h \
cbor/encoding.h \
cbor/floats_ctrls.h \
cbor/ints.h \
cbor/maps.h \
cbor/serialization.h \
cbor/streaming.h \
cbor/strings.h \
cbor/tags.h

INCSDIR=/usr/include

PKGCONFIG=libcbor

.SUFFIXES: .in

.in:
	${TOOL_SED} \
	    -e 's|@CMAKE_INSTALL_PREFIX@|/usr|g' \
	    -e 's|@CMAKE_INSTALL_LIBDIR@|/lib|g' \
	    -e 's|@PROJECT_NAME@|libcbor|g' \
	    -e 's|@CBOR_VERSION@|${CBOR_VERSION}|g' \
	    -e 's|@PROJECT_VERSION@|${CBOR_VERSION}|g' \
	    -e 's|@libdir_for_pc_file@|/usr/lib|g' \
	    -e 's|@includedir_for_pc_file@|/usr/include|g' ${.ALLSRC} > ${.TARGET}

libcbor.pc: libcbor.pc.in

.if defined(EMBER_LIBCBOR_SOURCE)
SHLIB_MAJOR=    1
SHLIB_MINOR=    0
.else
SHLIB_MAJOR=    0
SHLIB_MINOR=    5
.endif

cbor/configuration.h: ${VERS_FILE}
	${_MKTARGET_CREATE}
	mkdir -p cbor && ( \
	echo	'#define CBOR_MAJOR_VERSION ${CBOR_VERSION_MAJOR}' && \
	echo	'#define CBOR_MINOR_VERSION ${CBOR_VERSION_MINOR}' && \
	echo	'#define CBOR_PATCH_VERSION ${CBOR_VERSION_PATCH}' && \
	echo	'#define CBOR_CUSTOM_ALLOC 0' && \
	echo	'#define CBOR_MAX_STACK_SIZE 2048' && \
	echo	'#define CBOR_BUFFER_GROWTH 2' && \
	echo	'#define CBOR_PRETTY_PRINTER 1' && \
	echo	'#define CBOR_RESTRICT_SPECIFIER restrict' && \
	echo	'#define CBOR_INLINE_SPECIFIER ') > ${.TARGET}.tmp && \
	${MV} ${.TARGET}.tmp ${.TARGET}

.if defined(EMBER_LIBCBOR_SOURCE)
cbor/cbor_export.h:
	${_MKTARGET_CREATE}
	mkdir -p cbor && printf '%s\n' '#ifndef CBOR_EXPORT_H' '#define CBOR_EXPORT_H' \
	    '#define CBOR_EXPORT __attribute__((visibility("default")))' '#endif' > ${.TARGET}.tmp && \
	    ${MV} ${.TARGET}.tmp ${.TARGET}
CLEANFILES+= cbor/cbor_export.h cbor/cbor_export.h.tmp
.endif

CLEANFILES+= cbor/configuration.h
CLEANFILES+= cbor/configuration.h.tmp

.include <bsd.lib.mk>
