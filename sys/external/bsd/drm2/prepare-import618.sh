#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
#
# prepare-import618.sh LINUX_TREE REFDIR
#
# Import the Linux 6.18 DRM core into sys/external/bsd/drm2/dist618 (run from
# the top of the EmberBSD source tree), the way prepare-import.sh does for the
# 5.6 tree in dist, with these differences:
#
#  - the file set comes from the Linux Makefiles (drm618/tools/select618.py):
#    drivers/gpu/drm/*.c, display/, clients/, gpu/buddy.c, include/drm,
#    include/uapi/drm, include/linux/gpu_buddy.h, for an x86 PCI machine
#    without device tree, bridges, panels, MIPI and kunit;
#  - only files whose licence is MIT, BSD/ISC or "GPL-2.0 OR MIT" are copied
#    into dist618.  GPL-only files, files without a licence text and the uapi
#    headers under GPL-2.0 WITH Linux-syscall-note are NOT imported; they go to
#    REFDIR (outside the repository, for the compile map only) and are listed
#    in drm618/gpl-only.tsv as the list of files that need a BSD rewrite or a
#    decision of the owner;
#  - the i915 driver (drivers/gpu/drm/i915, include/drm/intel) is selected by
#    directory, not by its Makefile; gvt/, the self tests (selftests/ at any
#    level, selftest_*, mock_*, *_selftest*) are left out on purpose and listed
#    in drm618/i915-excluded.tsv (the GPL-licensed ones also in gpl-only.tsv,
#    with "excluded" in the last column);
#  - files are byte-identical to Linux 6.18.x; no RCS ids are inserted.  The
#    provenance is dist618/MANIFEST.tsv (sha256 of every file, licence class).
#    "sh prepare-import618.sh --verify LINUX_TREE" checks it;
#  - the local changes of imported files are drm618/patches/<path>.patch (one
#    unified diff of the Linux file per row of drm618/patches.tsv): they are
#    applied after the copy, and the import stops with a non-zero status if one
#    does not apply or does not give the sha256 recorded in patches.tsv.
#
# Layout of dist618 (the same relative layout as dist):
#   drm/*.c drm/*.h drm/display drm/clients   <- drivers/gpu/drm
#   gpu/buddy.c                               <- drivers/gpu/buddy.c
#   include/drm include/uapi/drm include/linux/gpu_buddy.h
# Outputs: dist618/MANIFEST.tsv dist618/LINUX_VERSION drm618/gpl-only.tsv, and
#          REFDIR/files.drmkms618.draft (the .c list in files(5) syntax; the
#          script reports its difference from drm618/files.drmkms618)

set -Ceu

D=sys/external/bsd/drm2
tools=$D/drm618/tools

if [ "${1:-}" = --verify ]; then
	lt=$2
	bad=0
	patched=0
	pt=$D/drm618/patches.tsv
	while IFS="	" read -r path sha cls spdx src; do
		[ "$path" = path ] && continue
		s=$(sha256sum "$lt/$src" | cut -d' ' -f1)
		[ "$s" = "$sha" ] || { echo "MISMATCH $path" >&2; bad=1; }
		d=$(sha256sum "$D/dist618/$path" | cut -d' ' -f1)
		if [ "$d" = "$sha" ]; then
			# Pristine.  A listed patch whose result is not in the file is not applied.
			if grep -q "^$path	" "$pt" 2>/dev/null; then
				echo "UNAPPLIED $path (listed in patches.tsv, file is pristine)" >&2
				bad=1
			fi
		else
			# Modified: allowed only as the listed local patch of exactly this Linux file.
			row=$(grep "^$path	" "$pt" 2>/dev/null || true)
			before=$(echo "$row" | cut -f2)
			after=$(echo "$row" | cut -f3)
			if [ -n "$row" ] && [ "$before" = "$sha" ] && [ "$after" = "$d" ]; then
				# the patch file applied to the Linux file must give exactly the tree file
				pf=$D/drm618/patches/$path.patch
				if [ ! -f "$pf" ]; then
					echo "ROW WITHOUT PATCH $path (drm618/patches/$path.patch is missing)" >&2
					bad=1
				else
					vtmp=$(mktemp)
					if patch -s -o "$vtmp" "$lt/$src" "$pf" >/dev/null 2>&1 && \
					    [ "$(sha256sum "$vtmp" | cut -d' ' -f1)" = "$d" ]; then
						:
					else
						echo "PATCH MISMATCH $path (original + patch is not the file of the tree)" >&2
						bad=1
					fi
					rm -f "$vtmp" "$vtmp.orig" "$vtmp.rej"
				fi
				patched=$((patched + 1))
				echo "PATCHED $path: $(echo "$row" | cut -f4)"
			else
				echo "MODIFIED $path (not a listed local patch)" >&2
				bad=1
			fi
		fi
	done < $D/dist618/MANIFEST.tsv
	# every row of patches.tsv must name a file of the manifest (a file
	# read in this shell, not in a pipeline: bad must reach the exit status)
	ptmp=$(mktemp)
	tail -n +2 "$pt" 2>/dev/null >| "$ptmp" || true
	while IFS="	" read -r path rest; do
		[ -n "$path" ] || continue
		if ! grep -q "^$path	" $D/dist618/MANIFEST.tsv; then
			echo "patches.tsv: $path is not imported" >&2
			bad=1
		fi
	done < "$ptmp"
	rm -f "$ptmp"
	# every patch file must have a row of patches.tsv
	if [ -d $D/drm618/patches ]; then
		ptmp=$(mktemp)
		( cd $D/drm618/patches && find . -name '*.patch' | sed 's|^\./||;s|\.patch$||' ) >| "$ptmp"
		while IFS= read -r path; do
			[ -n "$path" ] || continue
			if ! grep -q "^$path	" "$pt" 2>/dev/null; then
				echo "PATCH WITHOUT ROW drm618/patches/$path.patch (no row in patches.tsv)" >&2
				bad=1
			fi
		done < "$ptmp"
		rm -f "$ptmp"
	fi
	echo "verify: $patched file(s) of Linux 6.18 with a local patch (drm618/patches.tsv), rest pristine" >&2
	exit $bad
fi

lt=$1
ref=$2
[ -f "$lt/drivers/gpu/drm/drm_drv.c" ] || { echo "not a Linux tree: $lt" >&2; exit 2; }
# The reference directory is emptied below: it must be a place of its own,
# not the Linux tree, a parent or a child of it, the current directory or a
# parent of it, or the root.
# (a reference directory that does not exist yet is resolved through its
# first existing parent, and created only after these checks)
p=$ref; tail=
while [ ! -e "$p" ]; do tail=/$(basename "$p")$tail; p=$(dirname "$p"); done
ref_abs=$(cd "$p" && pwd -P)
ref_abs=${ref_abs%/}$tail
[ -n "$ref_abs" ] || ref_abs=/
lt_abs=$(cd "$lt" && pwd -P)
cwd_abs=$(pwd -P)
case "$ref_abs" in
/) echo "refusing to use / as the reference directory" >&2; exit 2 ;;
esac
for other in "$lt_abs" "$cwd_abs"; do
	case "$other/" in
	"$ref_abs"/*)
		echo "refusing: reference directory $ref_abs holds or is $other" >&2
		exit 2 ;;
	esac
	case "$ref_abs/" in
	"$other"/*)
		echo "refusing: reference directory $ref_abs is inside $other" >&2
		exit 2 ;;
	esac
done
# The version is the name of the tree directory (linux-6.18.63).
ver=$(basename "$(cd "$lt" && pwd)" | sed 's/^linux-//')
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

python3 -I $tools/select618.py "$lt" "$tmp" >/dev/null

# Destination of a Linux path, relative to dist618 / refdir.
dest() {
	case "$1" in
	drivers/gpu/drm/*)	echo "drm/${1#drivers/gpu/drm/}" ;;
	drivers/gpu/buddy.c)	echo "gpu/buddy.c" ;;
	include/*)		echo "$1" ;;
	esac
}

rm -rf $D/dist618 "$ref"
rm -f $D/drm618/gpl-only.tsv $D/drm618/i915-excluded.tsv
mkdir -p $D/dist618 "$ref"
printf 'path\tsha256\tclass\tspdx\tlinux-path\n' > $D/dist618/MANIFEST.tsv
printf 'path\tkind\tclass\tspdx\tbsd-rewrite-in-drm2\n' > $D/drm618/gpl-only.tsv
printf 'path\tkind\tclass\tspdx\treason\n' > $D/drm618/i915-excluded.tsv
echo "$ver" > $D/dist618/LINUX_VERSION

tail -n +2 "$tmp/select.tsv" | while IFS="	" read -r p kind cls spdx action reason; do
	d=$(dest "$p")
	case "$action" in
	import)
		mkdir -p "$D/dist618/$(dirname "$d")"
		cp "$lt/$p" "$D/dist618/$d"
		printf '%s\t%s\t%s\t%s\t%s\n' "$d" \
		    "$(sha256sum "$lt/$p" | cut -d' ' -f1)" "$cls" "$spdx" "$p" \
		    >> $D/dist618/MANIFEST.tsv
		;;
	skip)
		# Left out on purpose (excluded: ...): listed, not copied.
		case "$reason" in
		excluded:*)
			printf '%s\t%s\t%s\t%s\t%s\n' "$d" "$kind" "$cls" "$spdx" "$reason" \
			    >> $D/drm618/i915-excluded.tsv
			case "$cls" in
			MIT|DUAL) ;;
			*) printf '%s\t%s\t%s\t%s\texcluded\n' "$d" "$kind" "$cls" "$spdx" \
			    >> $D/drm618/gpl-only.tsv ;;
			esac
			;;
		esac
		;;
	ref)
		# NOREFCOPY=1: do not copy the GPL file anywhere, only list it
		# (clean-room work: nobody needs its text).
		if [ -z "${NOREFCOPY:-}" ]; then
			mkdir -p "$ref/$(dirname "$d")"
			cp "$lt/$p" "$ref/$d"
		fi
		b=$(basename "$d")
		old=-
		# An existing BSD rewrite has the same name, or the 5.6 name (gem_dma was gem_cma).
		for n in "$b" "$(echo "$b" | sed 's/gem_dma_helper/gem_cma_helper/;s/drm_hdcp_helper/drm_hdcp/')"; do
			[ -f "$D/drm/$n" ] && old="drm/$n"
		done
		printf '%s\t%s\t%s\t%s\t%s\n' "$d" "$kind" "$cls" "$spdx" "$old" \
		    >> $D/drm618/gpl-only.tsv
		;;
	esac
done

# The local patches of imported files.
pt=$D/drm618/patches.tsv
ptmp=$tmp/patch-rows
tail -n +2 "$pt" > "$ptmp"
applied=0
while IFS="	" read -r path sha_linux sha_patched purpose; do
	[ -n "$path" ] || continue
	pf=$D/drm618/patches/$path.patch
	[ -f "$pf" ] || { echo "no patch file $pf for $path (patches.tsv)" >&2; exit 3; }
	[ "$(sha256sum "$D/dist618/$path" | cut -d' ' -f1)" = "$sha_linux" ] || \
	    { echo "$path: the Linux file is not the one the patch was made for" >&2; exit 3; }
	patch -s -p1 -d $D/dist618 < "$pf" >| "$tmp/patch.out" 2>&1 || \
	    { echo "patch of $path does not apply:" >&2; cat "$tmp/patch.out" >&2; exit 3; }
	[ "$(sha256sum "$D/dist618/$path" | cut -d' ' -f1)" = "$sha_patched" ] || \
	    { echo "$path: the patched file is not the one recorded in patches.tsv" >&2; exit 3; }
	applied=$((applied + 1))
done < "$ptmp"
find $D/dist618 \( -name '*.orig' -o -name '*.rej' \) -delete
echo "applied $applied local patches (drm618/patches)" >&2

# Draft of the files list for config(1), next to the reference tree.
{
	printf '# Generated by prepare-import618.sh from Linux %s; edit files.drmkms618, not this draft.\n' "$ver"
	grep -E '^(drm|gpu)/.*\.c	' $D/dist618/MANIFEST.tsv | cut -f1 | grep -v '^drm/virtio/\|^drm/i915/' | sort | \
	while read -r f; do
		printf 'file\texternal/bsd/drm2/dist618/%s\tdrmkms618\n' "$f"
	done
} > "$ref/files.drmkms618.draft"
if [ -f $D/drm618/files.drmkms618 ]; then
	grep -h '^file.*dist618/' $D/drm618/files.drmkms618 | tr -s '\t' ' ' | sort > "$tmp/have"
	grep -h '^file' "$ref/files.drmkms618.draft" | tr -s '\t' ' ' | sort > "$tmp/want"
	diff "$tmp/have" "$tmp/want" > "$tmp/diff" || { echo "files.drmkms618 differs from the import:" >&2; cat "$tmp/diff" >&2; }
fi
echo "imported Linux $ver: $(grep -c . $D/dist618/MANIFEST.tsv) lines in MANIFEST.tsv, $(($(wc -l < $D/drm618/gpl-only.tsv) - 1)) files in gpl-only.tsv, $(($(wc -l < $D/drm618/i915-excluded.tsv) - 1)) in i915-excluded.tsv"
