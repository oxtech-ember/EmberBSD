#!/bin/sh
# Origin: EmberBSD (AI-assisted), test the production FFS superblock selector.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" = 1 ] || [ "$#" = 2 ] || {
	echo "Usage: $0 TOOLDIR [NEW_NATIVE_FIXTURES]" >&2; exit 2;
}
makefs=$1/bin/nbmakefs
[ -x "$makefs" ]
fixtures=${2-}
if [ -n "$fixtures" ]; then
	mkdir "$fixtures"
	fixtures=$(CDPATH= cd -- "$fixtures" && pwd)
fi
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${FFS_SOURCE_ROOT:-$(CDPATH= cd -- "$here/../.." && pwd)}
setup=$src/sbin/fsck_ffs/setup.c
header=$src/sys/ufs/ffs/fs.h
work=$(mktemp -d "${TMPDIR:-/tmp}/ffs-superblock.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
# Compile the actual function and exact on-disk struct layout. Keep the
# upstream notices with the extracted code, as in the other host contracts.
sed -n '1,/^#include /p' "$setup" | sed '/^#include /d' > "$work/detect.h"
printf 'static int\n' >> "$work/detect.h"
sed -n '/^detect_byteorder(/,/^}/p' "$setup" >> "$work/detect.h"
sed -n '1,/^#ifndef/p' "$header" | sed '/^#ifndef/d' > "$work/layout.h"
awk '$1 == "#define" && ($2 ~ /^(MAXMNTLEN|MAXVOLLEN|NOCSPTRS|FSMAXSNAP|FS_FLAGS_UPDATED|SBLOCKSIZE)$/ ||
    $2 ~ /^SBLOCK_(FLOPPY|UFS1|UFS2|PIGGY)$/ || $2 ~ /^FS_UFS[12]/) { print }' \
    "$header" >> "$work/layout.h"
sed -n '/^struct csum {/,/^};/p; /^struct csum_total {/,/^};/p; /^struct fs {/,/^};/p' \
    "$header" >> "$work/layout.h"
for mode in endian native; do
	define=
	[ "$mode" != native ] || define=-DNO_FFS_EI
	${CC:-cc} -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra -Werror $define \
	    ${FFS_TEST_CFLAGS:-} -I"$work" "$here/ffs-superblock-contract.c" -o "$work/$mode"
done
mkdir "$work/tree"
printf 'superblock selection fixture\n' > "$work/tree/marker"
for order in le be; do
	for extattr in 0 1; do
		image=$work/ufs2-$order-e$extattr.img
		"$makefs" -Z -B "$order" -t ffs -s 16m -o b=65536,f=8192,v=2,e=$extattr \
		    "$image" "$work/tree" > "$work/makefs.log"
		"$work/endian" "$image"
		if [ -n "$fixtures" ]; then
			cp "$image" "$fixtures/ufs2-$order-e$extattr.img"
			# Only the primary's fs_clean byte changes. The alternate
			# stays clean, reproducing the old fsck's false clean skip.
			printf '\000' | dd of="$fixtures/ufs2-$order-e$extattr.img" \
			    bs=1 seek=8401 count=1 conv=notrunc 2>/dev/null
		fi
	done
done
"$work/native"
echo 'PASS: makefs UFS2/UFS2EA primary selection, both byte orders, UFS1 and explicit backups'
