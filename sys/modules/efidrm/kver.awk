# SPDX-License-Identifier: BSD-2-Clause
# Write the version string of a kernel vers.c as EFIDRM_KERNEL_VERSION.
# newvers.sh writes it as one or more adjacent C string literals.
/^const char version\[\] =/ {
	found = 1
	sub(/^const char version\[\] =/, "")
}
found {
	text = text " " $0
}
found && /;[ \t]*$/ {
	sub(/;[ \t]*$/, "", text)
	print "/* Generated from the vers.c of the selected kernel. */"
	print "#define\tEFIDRM_KERNEL_VERSION" text
	done = 1
	exit
}
END {
	if (!done) {
		print "kver.awk: no version string in " FILENAME > "/dev/stderr"
		exit 1
	}
}
