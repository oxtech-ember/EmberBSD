#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# compile-map618.sh KOBJDIR REFDIR OUTDIR [dist618-relative .c files...]
#
# Compile every imported .c of dist618 on its own (clang -c, no link, not
# attached to any kernel) with the include path of the drmkms618 attribute and
# write OUTDIR/map.tsv: file, status (ok|error), number of errors, first error
# and its class (header|name|conflict|type|other).  The reference tree of the
# GPL-only files (prepare-import618.sh) is the last include directory; the
# headers found only there are the ones that need a BSD rewrite.
# KOBJDIR: a kernel build directory that holds the generated opt_*.h headers.
# Environment: CC, SYSROOT, S (the sys/ directory), J (parallel jobs, default 16),
# WARNFLAGS (default -w: warnings are not looked at; e.g. the kernel's
# "-Wall -Wextra -Wno-unused-parameter ..." to count them), OBJDIR (also keep the
# object files there, for the symbol list), EXTRA (more compiler flags, e.g. -include of a shim header).  The prefix header
# drm618/include/drm618_prefix.h is included as in files.drmkms618; NOPREFIX=1
# leaves it out to see what the sources need without it.  NOREF=1 leaves the
# reference tree (GPL headers) out, so a header only found there is reported as missing.
set -u

if [ "${1:-}" = --one ]; then
	f=$2
	D=$S/external/bsd/drm2
	log=$OUT/log/$(echo "$f" | tr / _).log
	src=$D/dist618/$f
	case $f in drm618/*) src=$D/$f ;; esac
	dd=$(dirname "$src"); rd=$(dirname "$REF/$f")
	# the one source that sees both kinds of struct file (drm618_file.h), as in files.drmkms618
	fx=
	case $f in drm618/drm618_file.c) fx=-DDRM618_NBSD_FILE ;; esac
	# shellcheck disable=SC2086
	$CC $FLAGS $fx -I$D/drm618/include -I$D/dist618/include -I$D/dist618/include/uapi \
	    -I$D/include -I$S/external/bsd/common/include -I$dd -I$rd \
	    ${REFINC} -c -o "${OBJDIR:-/dev/null}$([ -n "${OBJDIR:-}" ] && echo /$(echo "$f" | tr / _).o)" "$src" > "$log" 2>&1
	echo $? > "$log.rc"
	exit 0
fi

KOBJ=$1; REF=$2; OUT=$3; shift 3
S=${S:-$PWD/sys}
CC=${CC:-clang}
SYSROOT=${SYSROOT:?set SYSROOT to a destination directory with the target headers}
J=${J:-16}
D=$S/external/bsd/drm2
PFX="-include $D/drm618/include/drm618_prefix.h"
[ -n "${NOPREFIX:-}" ] && PFX=
FLAGS="--target=x86_64--netbsd -B$S/../ember/tools/no-external-as/ -O2 -std=gnu11 ${WARNFLAGS:--w} -ferror-limit=0
 -ffreestanding -fwrapv -fno-strict-aliasing -mno-red-zone -mno-mmx -mno-sse -mno-avx -msoft-float -mcmodel=kernel
 -fno-omit-frame-pointer --sysroot=$SYSROOT -Damd64 -Dx86_64 -I$KOBJ -I$S/external/mit/xen-include-public/dist/
 -I$S/external/bsd/libnv/dist -I$S/external/bsd/acpica/dist -I$S/external/bsd/acpica/dist/include -I$S/../common/include
 -I$S/arch -I$S -nostdinc -D__XEN_INTERFACE_VERSION__=0x3020a -DCOMPAT_44
 -D_KERNEL -D_KERNEL_OPT -D__KERNEL__ -DCONFIG_X86 -DCONFIG_X86_64 -DCONFIG_X86_PAT -DCONFIG_PCI=1

 ${PFX} ${EXTRA:-}"
REFINC="-I$REF/include -I$REF/include/uapi"
[ -n "${NOREF:-}" ] && REFINC=
export S CC FLAGS REF OUT REFINC
mkdir -p "$OUT/log"
if [ $# -gt 0 ]; then
	list=$*
else
	list=$(cd "$D/dist618" && grep -E '^(drm|gpu)/.*\.c	' MANIFEST.tsv | cut -f1 | sort)
	# the own sources of the core (drm618/*.c except the exclusion guard)
	list="$list $(cd "$D" && ls drm618/drm618_*.c | grep -v drm618_exclusive.c)"
	# Not built with CONFIG_DRM_FBDEV_EMULATION off (see files.drmkms618): listed in
	# drm618/tools/not-built.txt, "NOTBUILT=0" compiles them anyway.
	if [ "${NOTBUILT:-1}" = 1 ]; then
		list=$(for f in $list; do grep -qx "$f" "$D/drm618/tools/not-built.txt" || echo "$f"; done)
	fi
fi
for f in $list; do echo "$f"; done | xargs -P "$J" -I{} sh "$0" --one {}
printf 'file\tstatus\tnerr\tfirst-error\tclass\n' > "$OUT/map.tsv"
for f in $list; do
	log=$OUT/log/$(echo "$f" | tr / _).log
	rc=$(cat "$log.rc")
	if [ "$rc" = 0 ]; then
		printf '%s\tok\t0\t\t\n' "$f"
		continue
	fi
	n=$(grep -c 'error: ' "$log")
	e=$(grep -m1 'error: ' "$log" | sed "s|^[^ ]*/dist618/||;s|^[^ ]*/drm618/||;s|^[^ ]*/sys/||" | cut -c1-220)
	c=other
	case "$e" in
	*"file not found"*) c=header ;;
	*"undeclared identifier"*|*"undeclared function"*|*"implicit declaration"*|*"no member named"*|*"unknown type name"*|*"has no member"*) c=name ;;
	*"redefinition"*|*"conflicting types"*|*"redeclared"*) c=conflict ;;
	*"incompatible"*|*"too few"*|*"too many"*|*"invalid operands"*) c=type ;;
	esac
	printf '%s\terror\t%s\t%s\t%s\n' "$f" "$n" "$e" "$c"
done >> "$OUT/map.tsv"
