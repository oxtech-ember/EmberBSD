#!/bin/sh
# Host contract for the Linux style red-black tree of the Linux 6.18 DRM core
# (sys/external/bsd/drm2/drm618): see drm2-rbtree.c.  Pure C, needs only a
# host compiler.
# usage: drm2-rbtree.sh [CC]   (default: $CC, then clang, then cc)
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
root=$(CDPATH= cd -- "$here/../.." && pwd -P)
d=$root/sys/external/bsd/drm2/drm618
cc=${1:-${CC:-$(command -v clang || command -v cc)}}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm2-rbtree.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
flags="-std=gnu11 -O1 -g -Wall -Wextra -Wno-unused-parameter -Werror -I$d/include"
# shellcheck disable=SC2086
$cc $flags -o "$work/ok" "$here/drm2-rbtree.c" "$d/drm618_rbtree.c"
"$work/ok"
# Negative control: a failing check must fail the runner.
# shellcheck disable=SC2086
$cc $flags -DDRM2_RBTREE_SELFTEST_FAIL -o "$work/bad" "$here/drm2-rbtree.c" "$d/drm618_rbtree.c"
if "$work/bad" >"$work/bad.log" 2>&1; then
    echo 'negative control unexpectedly passed' >&2
    exit 1
fi
grep -q 'FAIL' "$work/bad.log" || { cat "$work/bad.log" >&2; exit 1; }
echo 'drm2-rbtree host contract: PASS'
