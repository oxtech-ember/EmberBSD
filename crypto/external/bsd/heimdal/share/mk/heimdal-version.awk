# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.

function literal_value(line, key, prefix) {
	prefix = key "='"
	if (index(line, prefix) != 1 || substr(line, length(line), 1) != "'")
		return ""
	return substr(line, length(prefix) + 1, length(line) - length(prefix) - 1)
}

/^PACKAGE_NAME=/ {
	name_count++
	package = literal_value($0, "PACKAGE_NAME")
}

/^PACKAGE_VERSION=/ {
	version_count++
	version = literal_value($0, "PACKAGE_VERSION")
}

END {
	if (name_count != 1 || version_count != 1 ||
	    package == "" || version == "" ||
	    package !~ /^[A-Za-z0-9._ +-]+$/ ||
	    version !~ /^[A-Za-z0-9._+-]+$/) {
		print "invalid literal PACKAGE_NAME/PACKAGE_VERSION in selected Heimdal configure" > "/dev/stderr"
		exit 1
	}
	printf "#ifndef VERSION_HIDDEN\n#define VERSION_HIDDEN\n#endif\n"
	printf "VERSION_HIDDEN const char *heimdal_long_version = \"@(#)$Version: %s %s $\";\n", package, version
	printf "VERSION_HIDDEN const char *heimdal_version = \"%s %s\";\n", package, version
}
