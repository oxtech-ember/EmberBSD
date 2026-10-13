#!/bin/sh
# Origin: EmberBSD (AI-assisted), read-only native fsck selection regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" = 4 ] || {
	echo "Usage: $0 BASELINE_FSCK FIXED_FSCK FIXTURES NEW_RESULTS" >&2; exit 2;
}
baseline=$1 fixed=$2 fixtures=$3 results=$4
[ "$(uname -s)" = NetBSD ]
[ -x "$baseline" ] && [ -x "$fixed" ]
[ -z "${LD_LIBRARY_PATH-}${LD_PRELOAD-}" ]
mkdir "$results"
fixtures=$(CDPATH= cd -- "$fixtures" && pwd)
results=$(CDPATH= cd -- "$results" && pwd)
uname -a > "$results/kernel.txt"
sha256 "$baseline" "$fixed" > "$results/fsck.sha256"
: > "$results/before.sha256"
: > "$results/after.sha256"
for order in le be; do
	for ea in 0 1; do
		name=ufs2-$order-e$ea
		image=$fixtures/$name.img
		[ -f "$image" ] && [ ! -L "$image" ]
		sha256 "$image" >> "$results/before.sha256"
		# -F restricts access to this ordinary image; -n forbids writes.
		"$baseline" -dFn "$image" > "$results/$name-old.log" 2>&1
		grep -q '^clean = 1$' "$results/$name-old.log"
		grep -q 'File system is clean; not checking' "$results/$name-old.log"
		"$fixed" -dFn "$image" > "$results/$name-fixed.log" 2>&1
		grep -q '^clean = 0$' "$results/$name-fixed.log"
		grep -q 'Phase 5 - Check Cyl groups' "$results/$name-fixed.log"
		! grep -q 'File system is clean; not checking' "$results/$name-fixed.log"
		for version in old fixed; do
			program=$baseline
			[ "$version" != fixed ] || program=$fixed
			"$program" -dFfn "$image" > "$results/$name-$version-forced.log" 2>&1
			grep -q 'Phase 5 - Check Cyl groups' "$results/$name-$version-forced.log"
		done
		sha256 "$image" >> "$results/after.sha256"
		printf 'PASS: %s old false-clean skip; fixed primary check; both forced checks\n' "$name"
	done
done
cmp "$results/before.sha256" "$results/after.sha256"
echo 'PASS: all fixture hashes unchanged; no device or installed utility was modified'
