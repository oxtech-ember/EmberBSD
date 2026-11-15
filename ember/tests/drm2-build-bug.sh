#!/bin/sh
# Host contract for drm618_build_bug.h (BUILD_BUG_ON decided after inlining).
# usage: drm2-build-bug.sh [CC]   (default: $CC, then clang, then cc)
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
root=$(CDPATH= cd -- "$here/../.." && pwd -P)
cc=${1:-${CC:-$(command -v clang || command -v cc)}}
work=$(mktemp -d "${TMPDIR:-/tmp}/drm2-build-bug.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
flags="-std=gnu11 -O2 -Wall -Wextra -Werror -I$root/sys/external/bsd/drm2/drm618/include"
# shellcheck disable=SC2086
$cc $flags -o "$work/ok" "$here/drm2-build-bug.c"
"$work/ok" >/dev/null
# Negative control: a true condition after inlining must stop the build.
# shellcheck disable=SC2086
if $cc $flags -DBUILD_BUG_NEGATIVE -o "$work/bad" "$here/drm2-build-bug.c" >"$work/bad.log" 2>&1; then
    echo 'negative control unexpectedly compiled' >&2
    exit 1
fi
grep -q 'BUILD_BUG_ON failed: p > 100' "$work/bad.log" || { cat "$work/bad.log" >&2; exit 1; }
echo 'drm2-build-bug host contract: PASS'
