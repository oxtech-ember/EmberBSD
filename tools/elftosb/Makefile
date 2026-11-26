# $NetBSD: Makefile,v 1.3 2018/05/19 17:48:51 christos Exp $

HOSTPROGNAME=	${_TOOL_PREFIX}elftosb
HOSTPROG_CXX=	${_TOOL_PREFIX}elftosb
HOST_SRCDIR=	external/bsd/elftosb/usr.sbin/elftosb

.include "${.CURDIR}/../Makefile.host"

# Use the C++ dialect selected by the elftosb sources.
HOST_CXXFLAGS+=	${CXXFLAGS:M-std=*:u}
