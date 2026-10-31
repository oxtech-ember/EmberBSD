# Preserve the IPC interface supplied by the native libkrb5 source list.
/^[[:space:]]*heim_(ipc_main|ipc_cred_get_(uid|gid|pid|session)|sipc_launchd_mach_init|sipc_service_unix);/ { duplicate = 1 }
/# kinit helper/ {
	if (++found != 1) {
		print "native version map: multiple kinit anchors" > "/dev/stderr"
		exit 1
	}
	print "\t\t# NetBSD native IPC exports"
	print "\t\theim_ipc_main;"
	print "\t\theim_ipc_cred_get_uid;"
	print "\t\theim_ipc_cred_get_gid;"
	print "\t\theim_ipc_cred_get_pid;"
	print "\t\theim_ipc_cred_get_session;"
	print "\t\theim_sipc_launchd_mach_init;"
	print "\t\theim_sipc_service_unix;"
	print ""
}
{ print }
END {
	if (found == 0) {
		print "native version map: missing kinit anchor" > "/dev/stderr"
		exit 1
	}
	if (duplicate) {
		print "native version map: IPC export already exists" > "/dev/stderr"
		exit 1
	}
}
