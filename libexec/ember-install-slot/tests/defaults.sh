#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tool=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cd "$work"
mkdir -p old/etc new/etc root/etc
for tree in old root; do
	printf 'original\n' >"$tree/etc/sys"
	printf 'same\n' >"$tree/etc/keep"
	printf 'secret\n' >"$tree/etc/private"
	printf 'old\n' >"$tree/etc/obsolete"
	printf 'first\n' >"$tree/etc/name"
	ln "$tree/etc/name" "$tree/etc/alias"
	ln -s sys "$tree/etc/ref"
done
echo local >root/etc/keep
chmod 0400 root/etc/private
echo upgrade >new/etc/sys
echo same >new/etc/keep
echo replacement >new/etc/private
chmod 0444 old/etc/private new/etc/private
echo second >new/etc/name
ln new/etc/name new/etc/alias
ln -s keep new/etc/ref
mkdir new/etc/added
echo child >new/etc/added/file
"$tool" "$PWD/root" "$PWD/old" "$PWD/new"
test "$(cat root/etc/sys)" = upgrade
test "$(cat root/etc/keep)" = local
test "$(cat root/etc/private)" = replacement
test "$(stat -f %Lp root/etc/private)" = 400
test ! -e root/etc/obsolete
test "$(readlink root/etc/ref)" = keep
test "$(stat -f %i root/etc/name)" = "$(stat -f %i root/etc/alias)"
test "$(cat root/etc/added/file)" = child
echo third >new/etc/name
rm root/etc/name
echo site-edit >root/etc/name
if "$tool" "$PWD/root" "$PWD/old" "$PWD/new"; then exit 1; fi
test "$(cat root/etc/name)" = site-edit
# A symlink in the cloned system cannot redirect the update to another mount.
rm -rf old new root
mkdir -p old/d new/d root outside
echo old >old/d/key
echo new >new/d/key
echo retained >outside/key
ln -s "$PWD/outside" root/d
if "$tool" "$PWD/root" "$PWD/old" "$PWD/new"; then exit 1; fi
test "$(cat outside/key)" = retained
echo 'PASS: defaults merge, private mode, site changes and hard links'
