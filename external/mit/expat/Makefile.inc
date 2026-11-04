#	$NetBSD: Makefile.inc,v 1.1 2012/02/29 20:07:56 tron Exp $

.include <bsd.own.mk>

EXPATDIR=	${NETBSDSRCDIR}/external/mit/expat
.if !defined(EMBER_EXPAT_SOURCE) && !(make(obj) && empty(.TARGETS:Nobj))
.error Set EMBER_EXPAT_SOURCE to the prepared common Ports source
.endif
EXPATSRCDIR=	${EMBER_EXPAT_SOURCE}

.for _LIB in expat
.if !defined(EXPATOBJDIR.${_LIB})
EXPATOBJDIR.${_LIB}!=	cd ${EXPATDIR}/lib/lib${_LIB} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	EXPATOBJDIR.${_LIB}
.endif
EXPATLIB.${_LIB}=	${EXPATOBJDIR.${_LIB}}/lib${_LIB}.a
.endfor
