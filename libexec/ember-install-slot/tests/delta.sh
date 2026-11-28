#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run the native delta test program on writable FFS as root.
set -eu
tool=$(realpath "$1")
parent=$2
stage=$(realpath "${3:-$(dirname "$0")/../stage-ffs}")
work=$(mktemp -d "$parent/delta.XXXXXXXX")
nested=
cleanup()
{
	if [ -n "$nested" ]; then umount "$nested" || exit 1; fi
	chflags -R 0 "$work" 2>/dev/null || :
	rm -rf "$work"
}
trap cleanup EXIT
rc=0; "$stage" "$work/invalid" /dev/missing prepare A plan || rc=$?
test "$rc" = 2
rc=0; "$stage" "$work/invalid" /dev/missing wrong prepare A plan || rc=$?
test "$rc" = 2
test ! -e "$work/invalid"
src=$work/source dst=$work/destination
mkdir "$src" "$dst" "$src/empty" "$src/tmp" "$src/obsolete" "$src/dir"
echo secret >"$work/outside"
dd if=/dev/urandom of="$src/same" bs=64k count=16 2>/dev/null
echo abcdefgh >"$src/content"
# Equal bytes and block counts must not conceal different sparse extents.
dd if=/dev/zero of="$src/sparse" bs=64k count=1 seek=2 2>/dev/null
dd if=/dev/zero of="$src/sparse" bs=64k count=1 seek=15 conv=notrunc 2>/dev/null
echo mode >"$src/mode"
echo owner >"$src/owner"
echo flags >"$src/flags"
echo old >"$src/obsolete/data"
echo hard >"$src/hard1"
ln "$src/hard1" "$src/hard2"
printf 'independent\n' >"$src/independent1"
cp -p "$src/independent1" "$src/independent2"
ln -s "$work" "$src/link"
mkfifo "$src/fifo"
printf 'line\n' >"$src/line
break"
chflags nodump "$src/flags"
(cd "$src" && tar -cpf "$work/full.tar" .)
(cd "$dst" && tar -xpf "$work/full.tar")
# Use an exact file-system time even when the archive time has lower precision.
for f in same content mode owner flags independent1 independent2; do
	touch -r "$src/$f" "$dst/$f"
done
inode=$(stat -f %i "$dst/same")
rm "$dst/sparse"
dd if=/dev/zero of="$dst/sparse" bs=64k count=1 seek=1 2>/dev/null
dd if=/dev/zero of="$dst/sparse" bs=64k count=1 seek=15 conv=notrunc 2>/dev/null
test "$(stat -f '%z:%b' "$src/sparse")" = "$(stat -f '%z:%b' "$dst/sparse")"
touch -r "$src/sparse" "$dst/sparse"
printf 'abcdXfgh\n' >"$dst/content"
touch -r "$src/content" "$dst/content"
chmod 000 "$dst/mode"
chown 1:1 "$dst/owner"
chflags 0 "$dst/flags"
rm "$dst/independent2"
ln "$dst/independent1" "$dst/independent2"
rm "$dst/hard2"
cp -p "$src/hard1" "$dst/hard2"
rm -r "$dst/dir"; ln -s "$work" "$dst/dir"
echo nested >"$src/dir/outside"
rm "$dst/link"; mkdir "$dst/link"; echo loose >"$dst/link/junk"
rm -r "$src/obsolete"; echo replacement >"$src/obsolete"
mkdir "$dst/stale"; ln -s "$work" "$dst/stale/escape"
attributes=
echo equal >"$src/attributes"
cp -p "$src/attributes" "$dst/attributes"
if setextattr -q user note original "$src/attributes" 2>/dev/null; then
	setextattr -q user note stale "$dst/attributes"
	attributes=yes
else
	rm "$src/attributes" "$dst/attributes"
fi
mkdir -p "$src/app/tmp" "$src/app/var/run" "$src/var/run" "$src/var/db/ember-update/incoming"
echo installed >"$src/app/tmp/required"
echo configured >"$src/app/var/run/site"
echo excluded >"$src/var/run/volatile"
echo excluded >"$src/var/db/ember-update/incoming/stale"
mkdir "$src/tmp/user" "$dst/tmp/old"
echo cached >"$dst/tmp/old/unused"
"$stage" snapshot-paths "$src" >"$work/all"
"$tool" "$src" "$dst" <"$work/all" >"$work/changed" 2>"$work/stats"
cat "$work/stats"
tr '\0' '\n' <"$work/changed" | grep -Fxq './sparse'
grep -Eq 'reused [1-9][0-9]* regular files .*1048576 bytes' "$work/stats"
(cd "$src" && tar -cpf "$work/delta.tar" --null --no-recursion -T "$work/changed")
(cd "$dst" && tar -xpf "$work/delta.tar")
test "$(stat -f %i "$dst/same")" = "$inode"
if [ -n "$attributes" ]; then
	test "$(getextattr -q user note "$dst/attributes")" = original
	echo 'Restored the user attribute on a supporting FFS.'
fi
cmp "$src/content" "$dst/content"
test "$(stat -f '%Lp:%u:%g' "$dst/mode" "$dst/owner")" = \
    "$(stat -f '%Lp:%u:%g' "$src/mode" "$src/owner")"
test "$(stat -f %f "$src/flags")" = "$(stat -f %f "$dst/flags")"
test "$(stat -f %i "$dst/hard1")" = "$(stat -f %i "$dst/hard2")"
test "$(stat -f %i "$dst/independent1")" != "$(stat -f %i "$dst/independent2")"
test "$(readlink "$dst/link")" = "$work"
cmp "$src/dir/outside" "$dst/dir/outside"
test "$(cat "$work/outside")" = secret
test -d "$dst/empty"
test -p "$dst/fifo"
cmp "$src/obsolete" "$dst/obsolete"
cmp "$src/line
break" "$dst/line
break"
test ! -e "$dst/stale"
test -d "$dst/tmp"
test -z "$(ls -A "$dst/tmp")"
mkdir "$work/reference"
(cd "$src" && tar --one-file-system -cpf "$work/reference.tar" --null --no-recursion -T "$work/all")
(cd "$work/reference" && tar -xpf "$work/reference.tar")
for t in "$work/reference" "$dst"; do
	test "$(cat "$t/app/tmp/required")" = installed
	test "$(cat "$t/app/var/run/site")" = configured
	test -d "$t/var/run"
	test ! -e "$t/var/run/volatile"
	test ! -e "$t/var/db/ember-update/incoming"
	test "$(stat -f %i "$t/hard1")" = "$(stat -f %i "$t/hard2")"
	cmp "$src/sparse" "$t/sparse"
done
for t in "$work/reference" "$dst"; do
	(cd "$t" && mtree -c -n -k type,uid,gid,mode,flags,nlink,size,link,sha256) >"$t.mtree"
done
cmp "$work/reference.mtree" "$dst.mtree"
# A flag that prevents unlink must send the caller back to the full-copy path.
echo locked >"$dst/locked"
chflags uchg "$dst/locked"
rc=0; "$tool" "$src" "$dst" <"$work/all" >"$work/changed" || rc=$?
test "$rc" = 3
chflags nouchg "$dst/locked"
mkdir "$dst/mount"
mount -t tmpfs tmpfs "$dst/mount"
nested=$dst/mount
echo protected >"$nested/data"
rc=0; "$tool" "$src" "$dst" <"$work/all" >"$work/changed" || rc=$?
test "$rc" = 1
test "$(cat "$nested/data")" = protected
umount "$nested"; nested=
echo 'Verified package slot reuse, tree replacement, metadata and fallback.'
