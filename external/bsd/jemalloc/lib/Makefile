# $NetBSD: Makefile,v 1.6 2025/01/31 15:25:23 christos Exp $

WARNS?= 5
.include <bsd.init.mk>

LIB=jemalloc

# The symbols from the stub only exist in newer jemalloc.
CPPFLAGS+=	-DHAVE_JEMALLOC=${HAVE_JEMALLOC:U0}
# EmberBSD: prefer the source-tree malloc.h over the stale toolchain
# include-fixed copy whose malloc_conf_get prototype differs.
CPPFLAGS+=	-I${NETBSDSRCDIR}/include
SRCS=		jemalloc_stub.c

.if ${HAVE_JEMALLOC:U0} <= 100
LIB_EXPSYM=	/dev/null
.endif

.include <bsd.lib.mk>
