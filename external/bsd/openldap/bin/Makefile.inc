#	$NetBSD: Makefile.inc,v 1.7 2021/08/14 16:14:49 christos Exp $

.include "../openldap.mk"

BINDIR=	/usr/bin

.PATH:	${LDAP_DISTDIR}/clients/tools
.PATH:	${LDAP_SRCDIR}/man

SRCS=	${PROG}.c
.if !defined(NO_COMMON_C)
SRCS+=	common.c
.endif


SRCS+=	version.c
CLEANFILES+= version.c

LDAP_VERSION_POLICY= ${.OBJDIR}/.openldap-version-policy
CLEANFILES+= ${LDAP_VERSION_POLICY} ${LDAP_VERSION_POLICY}.tmp
.if !((make(obj) || make(clean) || make(cleandir)) && empty(.TARGETS:Nobj:Nclean:Ncleandir))
# Invalidate before timestamp traversal: an epoch can change within one second.
# Variable-only metadata queries do not execute .BEGIN.
.BEGIN::
	@printf '%s\n' 'repro=${MKREPRO:Uno}' 'epoch=${SOURCE_DATE_EPOCH:U}' \
	    'version=${LDAP_VERSION}' > ${LDAP_VERSION_POLICY}.tmp
	@if cmp -s ${LDAP_VERSION_POLICY}.tmp ${LDAP_VERSION_POLICY}; then \
	    rm -f ${LDAP_VERSION_POLICY}.tmp; \
	else \
	    rm -f version.c version.o && \
	    mv -f ${LDAP_VERSION_POLICY}.tmp ${LDAP_VERSION_POLICY}; \
	fi
.endif
${LDAP_VERSION_POLICY}: .NOTMAIN

version.c: ${LDAP_SRCDIR}/openldap.mk ${LDAP_SRCDIR}/bin/Makefile.inc \
    ${LDAP_DISTDIR}/build/mkversion ${LDAP_DISTDIR}/build/version.sh \
    ${LDAP_DISTDIR}/build/version.var ${LDAP_VERSION_POLICY}
	${_MKTARGET_CREATE}
	${LDAP_MKVERSION} -s ${PROG} > ${.TARGET}


LDADD+=	-L${LDAPOBJDIR.lutil} -llutil
DPADD+=	${LDAPLIB.lutil}

LDADD+= ${LIBLDAP_LDADD}
DPADD+= ${LIBLDAP_DPADD}
