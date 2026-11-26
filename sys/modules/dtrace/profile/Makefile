# $NetBSD: Makefile,v 1.8 2019/02/17 04:05:48 rin Exp $

.include "../../Makefile.inc"

.PATH: ${NETBSDSRCDIR}/external/cddl/osnet/dev/profile

KMOD=		dtrace_profile
SRCS=		profile.c

CPPFLAGS+=	-I${NETBSDSRCDIR}/external/cddl/osnet/sys \
		-I${NETBSDSRCDIR}/external/cddl/osnet/dev/profile \
		-I${NETBSDSRCDIR}/external/cddl/osnet/dist/uts/common

CPPFLAGS+=	-Wno-unknown-pragmas -Wno-cast-qual

CPPFLAGS+=	${${ACTIVE_CC} == "gcc" :? -Wno-discarded-qualifiers :}
CPPFLAGS+=	${${ACTIVE_CC} == "clang" :? -Wno-incompatible-pointer-types-discards-qualifiers :}

WARNS=		3

# Clang 23 warnings in the CDDL DTrace code.
CPPFLAGS+=	${${ACTIVE_CC} == "clang" :? -Wno-strict-prototypes :}

.include <bsd.kmodule.mk>
