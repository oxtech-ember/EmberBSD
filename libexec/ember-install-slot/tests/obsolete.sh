#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# Run as root on NetBSD with the source stage-ffs and the previous signed base.
set -eu
[ "$#" -eq 2 ] && [ "$(id -u)" -eq 0 ] || exit 2
helper=$(realpath "$1")
archive=$(realpath "$2")
case_dir=$(mktemp -d /var/tmp/test-obsolete.XXXXXXXX)
trap 'rm -rf "$case_dir"' EXIT HUP INT TERM
cd "$case_dir"
mkdir -p root/usr/libexec root/etc/mtree next/etc/mtree next/var/db/obsolete
leaf=./usr/libexec/ember-os-update
tar -xpf "$archive" -C root "$leaf" ./etc/mtree/set.base
cp -p "root/$leaf" original
echo "$leaf" >next/var/db/obsolete/base
: >next/etc/mtree/set.base
tar -cf new.tar -C next .
pass() { "$helper" obsolete-base "$PWD/root" "$PWD/new.tar" "$PWD"; }
echo 'local service' >>"root/$leaf"
pass; test -e "root/$leaf"
cp -p original "root/$leaf"
chmod 0500 "root/$leaf"
pass; test -e "root/$leaf"
cp -p original "root/$leaf"
cp root/etc/mtree/set.base next/etc/mtree/set.base
tar -cf new.tar -C next .
pass; test -e "root/$leaf"
: >next/etc/mtree/set.base
: >next/var/db/obsolete/base
tar -cf new.tar -C next .
pass; test -e "root/$leaf"
echo "$leaf" >next/var/db/obsolete/base
tar -cf new.tar -C next .
pass; test ! -e "root/$leaf"
echo 'Retired the unchanged service only when its successor marked it obsolete.'
