# SPDX-License-Identifier: BSD-2-Clause
# Write the version and buildinfo strings of a kernel vers.c as
# EFIDRM_KERNEL_VERSION and EFIDRM_KERNEL_BUILDINFO. newvers.sh writes each
# as one or more adjacent C string literals. Two MKREPRO builds of one
# commit and configuration have the same version string; their BUILDINFO
# tells them apart.
function start(name) {
	current = name
	text[name] = ""
	sub("^const char " name "\\[\\] =", "")
}
/^const char version\[\] =/ { start("version") }
/^const char buildinfo\[\] =/ { start("buildinfo") }
current != "" {
	text[current] = text[current] " " $0
	if ($0 ~ /;[ \t]*$/) {
		sub(/;[ \t]*$/, "", text[current])
		done[current] = 1
		current = ""
	}
}
END {
	if (!done["version"] || !done["buildinfo"]) {
		print "kver.awk: no version and buildinfo strings in " FILENAME > "/dev/stderr"
		exit 1
	}
	print "/* Generated from the vers.c of the selected kernel. */"
	print "#define\tEFIDRM_KERNEL_VERSION" text["version"]
	print "#define\tEFIDRM_KERNEL_BUILDINFO" text["buildinfo"]
}
