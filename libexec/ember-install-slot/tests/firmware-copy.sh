#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Pass the copy test program and an empty, mounted 8 MiB test volume.
set -eu
tool=$1
volume=$2
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
dir=$volume/copytest
mkdir "$dir"
trap 'rm -rf "$dir" "$work"' EXIT
dd if=/dev/zero of="$dir/active" bs=1m count=3
cp "$dir/active" "$dir/inactive"
active=$(sha256 -q "$dir/active")
cp "$dir/active" "$work/kernel"
printf 'replacement' | dd of="$work/kernel" conv=notrunc
if LC_ALL=C "$tool" atomic "$work/kernel" "$dir/inactive" 2>"$work/failure"; then
	echo 'Test volume has room for a third kernel.' >&2
	exit 1
fi
grep -F "No space left on device" "$work/failure"
cmp "$dir/active" "$dir/inactive"
test "$(ls -A "$dir" | wc -l)" -eq 2
"$tool" inactive "$work/kernel" "$dir/inactive"
cmp "$work/kernel" "$dir/inactive"
dd if=/dev/zero of="$work/oversize" bs=1m count=8
if "$tool" inactive "$work/oversize" "$dir/inactive"; then exit 1; fi
test ! -e "$dir/inactive"
printf partial >"$dir/inactive"
"$tool" inactive "$work/kernel" "$dir/inactive"
cmp "$work/kernel" "$dir/inactive"
test "$(sha256 -q "$dir/active")" = "$active"
test "$(ls -A "$dir" | wc -l)" -eq 2
echo 'PASS: the inactive kernel fits, survives retry, and leaves the active kernel intact'
