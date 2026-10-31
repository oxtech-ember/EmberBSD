# $NetBSD: Makefile.inc,v 1.13.2.1 2025/11/20 18:46:44 martin Exp $

HEIMBASE?=	${NETBSDSRCDIR}/crypto/external/bsd/heimdal
# The common Ports source is shared with packages; keep native integration here.
.if !defined(EMBER_HEIMDAL_SOURCE) && empty(.MAKEFLAGS:M-V*) && !(make(obj) && empty(.TARGETS:Nobj)) && \
    !(make(clean) && empty(.TARGETS:Nclean)) && \
    !(make(cleandir) && empty(.TARGETS:Ncleandir))
.error Set EMBER_HEIMDAL_SOURCE to the prepared common Ports source
.endif
.if defined(EMBER_HEIMDAL_SOURCE) && !empty(EMBER_HEIMDAL_SOURCE)
HEIMDIST?=	${EMBER_HEIMDAL_SOURCE}
.if "${HEIMDIST}" != "${EMBER_HEIMDAL_SOURCE}"
.error HEIMDIST must match the prepared common Heimdal source
.endif
CPPFLAGS+=	-DEMBERBSD_BASE_HEIMDAL
HOST_CPPFLAGS+=	-DEMBERBSD_BASE_HEIMDAL
.else
HEIMDIST=	${HEIMBASE}/dist
.endif

SSLBASE?=	${NETBSDSRCDIR}/crypto/external/${EXTERNAL_OPENSSL_SUBDIR}

DPLIBASN1=	asn1 ${HEIMBASE}/lib/libasn1
DPLIBCOM_ERR=	com_err ${HEIMBASE}/lib/libcom_err
DPLIBGSSAPI=	gssapi ${HEIMBASE}/lib/libgssapi
DPLIBHDB=	hdb ${HEIMBASE}/lib/libhdb
DPLIBHEIMBASE=	heimbase ${HEIMBASE}/lib/libheimbase
DPLIBHEIMNTLM=	heimntlm ${HEIMBASE}/lib/libheimntlm
DPLIBHX509=	hx509 ${HEIMBASE}/lib/libhx509
DPLIBIPC=	ipc ${HEIMBASE}/lib/libipc
DPLIBKADM5CLNT=	kadm5clnt ${HEIMBASE}/lib/libkadm5clnt
DPLIBKADM5SRV=	kadm5srv ${HEIMBASE}/lib/libkadm5srv
DPLIBKAFS=	kafs ${HEIMBASE}/lib/libkafs
DPLIBKDC=	kdc ${HEIMBASE}/lib/libkdc
DPLIBKRB5=	krb5 ${HEIMBASE}/lib/libkrb5
DPLIBROKEN=	roken ${HEIMBASE}/lib/libroken
DPLIBSL=	sl ${HEIMBASE}/lib/libsl
DPLIBVERS=	vers ${HEIMBASE}/lib/libvers
DPLIBWIND=	wind ${HEIMBASE}/lib/libwind
KRB5LDADD=	-lcrypto -lcrypt
KRB5DPADD=	${LIBCRYPTO} ${LIBCRYPT}
HDBLDADD=	-lsqlite3 -lm
HDBDPADD=	${LIBSQLITE3} ${LIBM}

.if ${USETOOLS} != "yes"
COMPILEETOBJ!=     cd ${HEIMBASE}/lib/libcom_err/compile_et && ${PRINTOBJDIR}
TOOL_COMPILE_ET=   ${COMPILEETOBJ}/compile_et

ASN1COMPILEOBJ!=   cd ${HEIMBASE}/lib/libasn1/asn1_compile && ${PRINTOBJDIR}
TOOL_ASN1_COMPILE= ${ASN1COMPILEOBJ}/asn1_compile

SLCOBJ!=	cd ${HEIMBASE}/lib/libsl/slc && ${PRINTOBJDIR}
TOOL_SLC=	${SLCOBJ}/slc
.endif

#
# We now define what are standard CPP flags across Heimdal:

.if (${USE_INET6} != "no")
CPPFLAGS+=-DHAVE_IPV6
HOST_CPPFLAGS+=-DHAVE_IPV6
.endif

.if ${MKPIC} == "no"
CPPFLAGS+=	-DNO_DLOPEN
.endif

CPPFLAGS+=	-DHAVE_CONFIG_H
HOST_CPPFLAGS+=	-DHAVE_CONFIG_H

CPPFLAGS+= ${.PATH:S/^/-I/}
CPPFLAGS+= -I${HEIMBASE}/include
CPPFLAGS+= -I${HEIMDIST}/include
CWARNFLAGS.clang+=	-Wno-error=absolute-value

.if defined(EMBER_HEIMDAL_SOURCE) && !empty(EMBER_HEIMDAL_SOURCE) && !defined(HOSTPROGNAME)
CPPFLAGS+=	-I${DESTDIR}/usr/include/krb5 -I${DESTDIR}/usr/include/gssapi
.endif
