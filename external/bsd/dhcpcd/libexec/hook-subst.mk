# Generate the dhcpcd scripts imported as templates in the selected Ports source.
DHCPCD_CONFIG=${SRCDIR}/include/config.h
DHCPCD_SYSCONFDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*SYSCONFDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_LIBDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*LIBDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_LIBEXECDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*LIBEXECDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_DBDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*DBDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
DHCPCD_RUNDIR!=	${TOOL_SED} -n -e 's/^[[:space:]]*\#define[[:space:]]*RUNDIR[[:space:]]*"\([^"]*\)".*/\1/p' ${DHCPCD_CONFIG}
.if empty(DHCPCD_SYSCONFDIR) || empty(DHCPCD_LIBDIR) || empty(DHCPCD_LIBEXECDIR) || empty(DHCPCD_DBDIR) || empty(DHCPCD_RUNDIR)
.error Missing dhcpcd paths in ${DHCPCD_CONFIG}
.endif
LIBEXECDIR=${DHCPCD_LIBEXECDIR}
SCRIPT=${DHCPCD_LIBEXECDIR}/dhcpcd-run-hooks
HOOKDIR=${DHCPCD_LIBEXECDIR}/dhcpcd-hooks
DHCPCD_HOOK_SUBST=-e 's|@RUNDIR@|${DHCPCD_RUNDIR}|g' -e 's|@DBDIR@|${DHCPCD_DBDIR}|g' -e 's|@LIBDIR@|${DHCPCD_LIBDIR}|g' -e 's|@HOOKDIR@|${HOOKDIR}|g' -e 's|@SERVICEEXISTS@||g' -e 's|@SERVICECMD@||g' -e 's|@SERVICESTATUS@||g' -e 's|@STATUSARG@||g' -e 's|@SCRIPT@|${SCRIPT}|g' -e 's|@SYSCONFDIR@|${DHCPCD_SYSCONFDIR}|g'
DHCPCD_HOSTNAME_SUBST=-e 's|@DEFAULT_HOSTNAME@||g'
