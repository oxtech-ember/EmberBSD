#	$NetBSD: openldap.mk,v 1.8 2021/08/14 16:14:49 christos Exp $

.include <bsd.own.mk>

# EmberBSD: OpenLDAP ac/*.h K&R () conflict with libc under Clang -Werror.
CWARNFLAGS.clang+=	-Wno-error=deprecated-non-prototype

LDAP_SRCDIR=	${NETBSDSRCDIR}/external/bsd/openldap
.if !defined(EMBER_OPENLDAP_SOURCE) && !(make(obj) && empty(.TARGETS:Nobj))
.error Set EMBER_OPENLDAP_SOURCE to the prepared common Ports source
.endif
.if defined(EMBER_OPENLDAP_SOURCE)
LDAP_DISTDIR=	${EMBER_OPENLDAP_SOURCE}
.if make(obj) && empty(.TARGETS:Nobj)
LDAP_VERSION=	0.0.0
.else
LDAP_VERSION_INFO!= ${HOST_SH} ${LDAP_DISTDIR}/build/version.sh
LDAP_VERSION=	${LDAP_VERSION_INFO:MOL_VERSION=*:S/OL_VERSION=//}
LDAP_MAJOR=	${LDAP_VERSION_INFO:MOL_MAJOR=*:S/OL_MAJOR=//}
LDAP_MINOR=	${LDAP_VERSION_INFO:MOL_MINOR=*:S/OL_MINOR=//}
LDAP_PATCH=	${LDAP_VERSION_INFO:MOL_PATCH=*:S/OL_PATCH=//}
LDAP_API_INC=	${LDAP_VERSION_INFO:MOL_API_INC=*:S/OL_API_INC=//}
.if empty(LDAP_VERSION) || empty(LDAP_MAJOR) || empty(LDAP_MINOR) || empty(LDAP_PATCH) || empty(LDAP_API_INC)
.error Cannot determine version of the prepared common OpenLDAP source
.endif
.endif
CPPFLAGS+=	-I${.OBJDIR}
DPSRCS+=	libldap-config.h liblber-config.h ${.OBJDIR}/ldap_features.h
CLEANFILES+=	libldap-config.h liblber-config.h ldap_features.h
libldap-config.h: ${LDAP_DISTDIR}/include/libldap-config.hin
	cp ${.ALLSRC} ${.TARGET}
liblber-config.h: ${LDAP_DISTDIR}/include/liblber-config.hin
	cp ${.ALLSRC} ${.TARGET}
${.OBJDIR}/ldap_features.h: ${LDAP_DISTDIR}/include/ldap_features.hin ${LDAP_DISTDIR}/build/version.var
	sed -e 's/^#undef LDAP_VENDOR_VERSION$$/#define LDAP_VENDOR_VERSION ${LDAP_API_INC}/' \
	    -e 's/^#undef LDAP_VENDOR_VERSION_MAJOR$$/#define LDAP_VENDOR_VERSION_MAJOR ${LDAP_MAJOR}/' \
	    -e 's/^#undef LDAP_VENDOR_VERSION_MINOR$$/#define LDAP_VENDOR_VERSION_MINOR ${LDAP_MINOR}/' \
	    -e 's/^#undef LDAP_VENDOR_VERSION_PATCH$$/#define LDAP_VENDOR_VERSION_PATCH ${LDAP_PATCH}/' \
	    -e 's/^#undef LDAP_API_FEATURE_X_OPENLDAP_REENTRANT$$/#define LDAP_API_FEATURE_X_OPENLDAP_REENTRANT 1/' \
	    -e 's/^#undef LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE$$/#define LDAP_API_FEATURE_X_OPENLDAP_THREAD_SAFE 1/' \
	    ${LDAP_DISTDIR}/include/ldap_features.hin > ${.TARGET}
.else
LDAP_VERSION=	0.0.0
LDAP_DISTDIR=	${NETBSDSRCDIR}/external/bsd/openldap/dist
.endif

LDAP_PREFIX=	/usr

LDAP_DATADIR=	${LDAP_PREFIX}/share/openldap
LDAP_ETCDIR=	/etc
LDAP_RUNDIR=	/var/openldap

CPPFLAGS+=	-I${LDAP_SRCDIR}/include
CPPFLAGS+=	-I${LDAP_DISTDIR}/include

.for _LIB in lutil ldap		# XXX lber ldap_r lunicode rewrite
.if !defined(LDAPOBJDIR.${_LIB})
LDAPOBJDIR.${_LIB}!=	cd ${LDAP_SRCDIR}/lib/lib${_LIB} && ${PRINTOBJDIR}
.MAKEOVERRIDES+=	LDAPOBJDIR.${_LIB}
.endif
LDAPLIB.${_LIB}=	${LDAPOBJDIR.${_LIB}}/lib${_LIB}.a
.endfor

LDAP_MKVERSION=	${HOST_SH} ${LDAP_DISTDIR}/build/mkversion -v "${LDAP_VERSION}"

CPPFLAGS+=	-DHAVE_TLS
