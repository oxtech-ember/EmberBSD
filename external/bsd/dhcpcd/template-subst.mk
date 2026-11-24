# Generate dhcpcd files from the templates in the selected Ports source.
# The paths come from the EmberBSD config.h.
DHCPCD_CONFIG=${SRCDIR}/include/config.h
DHCPCD_SYSCONFDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*SYSCONFDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_LIBDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*LIBDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_LIBEXECDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*LIBEXECDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_DBDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*DBDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_RUNDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*RUNDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
.if empty(DHCPCD_SYSCONFDIR) || empty(DHCPCD_LIBDIR) || empty(DHCPCD_LIBEXECDIR) || empty(DHCPCD_DBDIR) || empty(DHCPCD_RUNDIR)
.error Missing dhcpcd paths in ${DHCPCD_CONFIG}
.endif
DHCPCD_SCRIPT=${DHCPCD_LIBEXECDIR}/dhcpcd-run-hooks
DHCPCD_HOOKDIR=${DHCPCD_LIBEXECDIR}/dhcpcd-hooks
# examples/hooks installs the optional hooks below this directory.
DHCPCD_DATADIR=/usr/share/examples
DHCPCD_SUBST=-e 's|@RUNDIR@|${DHCPCD_RUNDIR}|g' -e 's|@DBDIR@|${DHCPCD_DBDIR}|g' -e 's|@LIBDIR@|${DHCPCD_LIBDIR}|g' -e 's|@HOOKDIR@|${DHCPCD_HOOKDIR}|g' -e 's|@DATADIR@|${DHCPCD_DATADIR}|g' -e 's|@SERVICEEXISTS@||g' -e 's|@SERVICECMD@||g' -e 's|@SERVICESTATUS@||g' -e 's|@STATUSARG@||g' -e 's|@SCRIPT@|${DHCPCD_SCRIPT}|g' -e 's|@SYSCONFDIR@|${DHCPCD_SYSCONFDIR}|g'
DHCPCD_HOSTNAME_SUBST=-e 's|@DEFAULT_HOSTNAME@||g'

# The prepared source has manual page templates, not generated pages.
# Set MAN and DHCPCD_MANSRC (the template directory) before this file
# is included. The loop below reads MAN when make parses this file.
.for _m in ${MAN}
CLEANFILES+=	${_m} ${_m}.tmp
${_m}: ${DHCPCD_MANSRC}/${_m}.in ${DHCPCD_CONFIG}
	${TOOL_SED} ${DHCPCD_SUBST} ${DHCPCD_MANSRC}/${_m}.in > ${.TARGET}.tmp
	${TOOL_AWK} '/@[A-Z_]+@/ { print FILENAME ": unknown substitution: " $$0; bad = 1 } END { exit bad }' ${.TARGET}.tmp
	mv ${.TARGET}.tmp ${.TARGET}
.endfor
