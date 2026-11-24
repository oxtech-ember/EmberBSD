#!/bin/sh
# Check that kver.awk writes the version string of a kernel vers.c that
# sys/conf/newvers.sh made, and that efidrm then refuses another kernel.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/efidrm-kver.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk=${AWK:-awk}
kver=$src/sys/modules/efidrm/kver.awk

# The module accepts a kernel as efidrm_modcmd() does: the version string
# starts with EFIDRM_KERNEL_VERSION. The module of the same kernel also has
# the whole version string.
cat > "$work/check.c" <<'EOF'
#include <stdio.h>
#include <string.h>
#include "efidrm_kver.h"

extern const char version[];

int
main(int argc __attribute__((unused)), char **argv)
{
	int accept = strncmp(version, EFIDRM_KERNEL_VERSION,
	    sizeof(EFIDRM_KERNEL_VERSION) - 1) == 0;
	int whole = strcmp(version, EFIDRM_KERNEL_VERSION) == 0;
	int same = strcmp(argv[1], "same") == 0;

	if (accept != same || whole != same) {
		fprintf(stderr, "kernel \"%s\", module \"%s\": expected %s\n",
		    version, EFIDRM_KERNEL_VERSION, argv[1]);
		return 1;
	}
	return 0;
}
EOF

# newvers.sh does not quote its own path: run copies from the work directory.
mkdir -p "$work/sys/conf" "$work/sys/sys"
cp "$src/sys/conf/newvers.sh" "$src/sys/conf/osrelease.sh" \
    "$src/sys/conf/copyright" "$work/sys/conf/"
cp "$src/sys/sys/param.h" "$work/sys/sys/"

# kernel NAME NEWVERS-OPTIONS: write vers.c of the kernel NAME.
kernel()
{
	name=$1
	shift
	mkdir "$work/$name"
	(cd "$work/$name" && TOOL_AWK=$awk sh "$work/sys/conf/newvers.sh" \
	    -n -i "$name" "$@")
	test -s "$work/$name/vers.c"
	# The host compiler has no kernel headers; the strings do not use them.
	grep -v '^#include' "$work/$name/vers.c" > "$work/$name/strings.c"
}

# check KERNEL MODULE same|other: build the module guard for the kernel
# MODULE and run it in the kernel KERNEL.
check()
{
	"$awk" -f "$kver" "$work/$2/vers.c" > "$work/$2/efidrm_kver.h"
	grep -q '^#define	EFIDRM_KERNEL_VERSION  *"' "$work/$2/efidrm_kver.h"
	${CC:-cc} ${CFLAGS:-} -std=c11 -Wall -Wextra -Werror -I"$work/$2" \
	    "$work/check.c" "$work/$1/strings.c" ${LDFLAGS:-} -o "$work/check"
	"$work/check" "$3"
}

# lines KERNEL: write the number of lines of the version declaration.
lines()
{
	"$awk" '/^const char version\[\] =/ { n = 1 }
	    n && /;$/ { print n; exit }
	    n { n++ }' "$work/$1/vers.c"
}

# A build without MKREPRO has a version string of two lines: newvers.sh
# writes it as adjacent string literals on two lines.
kernel GENERIC_ARL
test "$(lines GENERIC_ARL)" -eq 2
check GENERIC_ARL GENERIC_ARL same
# A reproducible build has a version string of one line.
kernel EMBER64 -R
test "$(lines EMBER64)" -eq 1
check EMBER64 EMBER64 same
# A module for another kernel does not attach.
check GENERIC_ARL EMBER64 other
check EMBER64 GENERIC_ARL other

# A file without a version string makes no header.
printf 'const char ostype[] = "NetBSD";\n' > "$work/empty.c"
if "$awk" -f "$kver" "$work/empty.c" > "$work/empty.h" 2> "$work/empty.err"; then
	echo "kver.awk accepted a file without a version string" >&2
	exit 1
fi
grep -q 'no version string' "$work/empty.err"
echo "efidrm-kver: ok"
